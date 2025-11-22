/*
 * This file is part of stsmon - a simple DVB transport stream monitor
 * Copyright (C) 2025 Michał Podsiadlik <michal@nglab.net>
 *
 * stsmon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * stsmon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with stsmon. If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif
#include <sys/time.h>
#include <stdbool.h>
#include <signal.h>
#include <inttypes.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/psi.h>
#include <bitstream/mpeg/psi/pat.h>
#include <bitstream/mpeg/psi/pmt.h>
#include <bitstream/dvb/si/sdt.h>
#pragma GCC diagnostic pop
#include "pid.h"
#include "services.h"
#include "output.h"

extern int show_cc;
extern int show_times;
extern int quiet_mode;
extern const char *csv_file;

/* Return monotonic-ish wallclock time in microseconds.
 * Used for packet timing, deltas and statistics intervals.
 */
uint64_t tsusecs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}

uint64_t sync_errors = 0;
uint64_t cc_errors = 0;
uint64_t tei_errors = 0;
uint64_t packets_all = 0;
uint64_t packets_data = 0;

ts_pid_t pid_table[TS_MAX_PID];

extern void handle_pat_section(uint16_t pid, uint8_t *section);
extern void handle_sdt_section(uint16_t pid, uint8_t *section);
extern void handle_pmt(uint16_t pid, uint8_t *section);

extern void pat_cleanup();
extern void sdt_cleanup();

static void handle_section(uint16_t pid, uint8_t *section)
{
    uint8_t table_id = psi_get_tableid(section);
    if (!psi_validate(section))
    {
        out_log(LogLevel_Error, "Invalid section on PID %u", pid);
        free(section);
        return;
    }
    /*
     * Dispatch a fully assembled PSI/SI section to the appropriate
     * handler based on its table id. Note ownership rules:
     * - `section` is a heap buffer returned by `psi_assemble_payload`.
     * - Handlers take ownership of `section` and must free it if they
     *   do not keep a reference to it (see `handle_pat_section`, etc.).
     * - For unknown table ids we free the section here to avoid leaks.
     */
    switch (table_id)
    {
    case PAT_TABLE_ID:
        handle_pat_section(pid, section);
        break;
    case PMT_TABLE_ID:
        handle_pmt(pid, section);
        break;
    case SDT_TABLE_ID_ACTUAL:
        handle_sdt_section(pid, section);
        break;
    default:
        // Unhandled table
        free(section);
        break;
    }
}

volatile sig_atomic_t terminate = 0;

static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM)
    {
        terminate = 1;
    }
}

static int socketErrno()
{
#ifdef WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static const char* socketStrError(int err)
{
#ifdef WIN32
    static char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   buf, sizeof(buf), NULL);
    return buf;
#else
    return strerror(err);
#endif
}

int monitor_stream(const char *multicast_addr, uint16_t port, const char *local_interface)
{
#ifdef WIN32
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        out_log(LogLevel_Error, "WSAStartup failed: %d", iResult);
        return 1;
    }
#endif
    // Initialize PID table
    for (int i = 0; i < TS_MAX_PID; i++)
    {
        pid_table[i].last_cc = 0xFF;
        pid_table[i].packets = 0;
        pid_table[i].psi_buffer = NULL;
        pid_table[i].psi_buffer_used = 0;
        pid_table[i].is_psi = false;
    }
    pid_table[PAT_PID].is_psi = true; // PAT PID
    pid_table[SDT_PID].is_psi = true; // SDT PID

    out_log(LogLevel_Info, "Monitoring stream at %s:%d", multicast_addr, port);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        out_log(LogLevel_Error, "socket() failed: %s (%d)", socketStrError(socketErrno()), socketErrno());
        return 1;
    }
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt)) < 0)
    {
        out_log(LogLevel_Warning, "setsockopt(SO_REUSEADDR) failed: %s (%d)", socketStrError(socketErrno()), socketErrno());        
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        out_log(LogLevel_Error, "bind(%s:%d) failed: %s (%d)", multicast_addr, port, socketStrError(socketErrno()), socketErrno());
        close(fd);
        return 1;
    }

    struct ip_mreq mreq;
    struct in_addr group_addr;
    group_addr.s_addr = inet_addr(multicast_addr);
    if (group_addr.s_addr == INADDR_NONE)
    {
        out_log(LogLevel_Error, "invalid multicast address '%s'", multicast_addr);
        close(fd);
        return 1;
    }
    mreq.imr_multiaddr = group_addr;
    /* If a local interface IP was provided, use it; otherwise use INADDR_ANY */
    if (local_interface && local_interface[0] != '\0')
    {
        struct in_addr iface_addr;
        iface_addr.s_addr = inet_addr(local_interface);
        if (iface_addr.s_addr == INADDR_NONE)
        {
            out_log(LogLevel_Error, "invalid local interface address '%s'", local_interface);
            close(fd);
            return 1;
        }
        mreq.imr_interface = iface_addr;
    }
    else
    {
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    }
#if defined(WIN32)
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq)) < 0)
#else
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const void *)&mreq, sizeof(mreq)) < 0)
#endif
    {
        out_log(LogLevel_Error, "setsockopt(IP_ADD_MEMBERSHIP) failed: %s (%d)", socketStrError(socketErrno()), socketErrno());
        close(fd);
        return 1;
    }
    uint64_t last_ts = tsusecs();
    uint64_t start_ts = 0;
    uint64_t last_stats = last_ts;

    uint64_t last_packet_count = 0;
    uint64_t last_sync_errors = 0;
    uint64_t last_cc_errors = 0;
    uint64_t last_tei_errors = 0;
    uint64_t last_packets_data = 0;

// Install signal handlers
#ifndef WIN32
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
#endif
    FILE *log_file = NULL;
    if (csv_file)
    {
        if (strcmp(csv_file, "-") == 0)
        {
            log_file = stdout;
        }
        else
        {
            log_file = fopen(csv_file, "a");
            if (!log_file)
            {
                out_log(LogLevel_Error, "fopen('%s') failed: %s (%d)", csv_file, strerror(errno), errno);
                close(fd);
                return 1;
            }
        }
    }

    if (log_file)
        fprintf(log_file, "Timestamp,Bitrate (kbps),Data Bitrate (kbps),CC Errors,Sync Errors,TEI Errors,Total Packets,Data Packets\n");

    while (1)
    {
        if (terminate)
        {
            break;
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int ret = select(fd + 1, &read_fds, NULL, NULL, &timeout);
        uint64_t now = tsusecs();

        if (ret < 0)
        {
            if (errno == EINTR)
                continue;

            out_log(LogLevel_Error, "select() failed: %s (%d)", socketStrError(socketErrno()), socketErrno());
            break;
        }
        else if (ret == 0)
        {
            // Timeout
            goto stats_print;
        }

        struct sockaddr_in src_addr;
        socklen_t addrlen = sizeof(src_addr);
        char buffer[2048];
        ssize_t nbytes = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&src_addr, &addrlen);

        if (nbytes < 0)
        {
            out_log(LogLevel_Error, "recvfrom() failed: %s (%d)", socketStrError(socketErrno()), socketErrno());
            break;
        }

        if (start_ts == 0)
            start_ts = now;

        uint64_t delta = now - last_ts;

        if (show_times)
        {
            out_timestamp();
            if (delta > 1000000)
                out_color(COLOR_RED);
            else if (delta > 500000)
                out_color(COLOR_YELLOW);
            else
                out_color(COLOR_GREEN);
            printf(" Packet received (delta %" PRIu64 " us)\n", delta);
            out_reset();
        }
        else if (delta > 1000000)
        {
            out_timestamp();
            out_color(delta > 1000000 ? COLOR_RED : COLOR_YELLOW);
            printf(" %s: Packet gap detected, last packet was %.2f s ago\n",
                   delta > 1000000 ? "Error" : "Warning", (double)delta / 1000000.0);
            out_reset();
        }

        last_ts = now;

        for (ssize_t i = 0; i + TS_SIZE <= nbytes; i += TS_SIZE)
        {
            uint8_t *ts_packet = (uint8_t *)(buffer + i);

            packets_all++;

            if (!ts_validate(ts_packet))
            {
                sync_errors++;
                continue;
            }

            uint16_t pid = ts_get_pid(ts_packet);
            if (pid == TS_MAX_PID)
            {
                continue; // Ignore null packets
            }

            packets_data++;

            ts_pid_t *pe = &pid_table[pid];
            uint8_t cc = ts_get_cc(ts_packet);
            bool had_errors = false;
            if (pe->last_cc != 0xFF)
            {
                if (ts_check_discontinuity(cc, pe->last_cc))
                {
                    cc_errors++;
                    had_errors = true;
                    if (show_cc)
                    {
                        out_timestamp();
                        out_color(COLOR_YELLOW);
                        printf(" Discontinuity detected on PID %u: last CC %u, current CC %u\n", pid, pe->last_cc, cc);
                        out_reset();
                    }
                }
            }
            pe->last_cc = cc;

            pe->packets++;

            if (ts_get_transporterror(ts_packet))
            {
                had_errors = true;
                tei_errors++;
            }

            if (pe->is_psi)
            {
                if (had_errors)
                {
                    // Reset PSI collection on error
                    psi_assemble_reset(&pe->psi_buffer, &pe->psi_buffer_used);
                    continue;
                }

                uint8_t *payload = ts_section(ts_packet);
                uint8_t payload_length = TS_SIZE - (payload - ts_packet);

                uint8_t *section = psi_assemble_payload(&pe->psi_buffer, &pe->psi_buffer_used,
                                                        (const uint8_t **)&payload, &payload_length);
                if (section)
                {
                    if (!psi_validate(section))
                    {
                        // Invalid PSI section, discard
                        psi_assemble_reset(&pe->psi_buffer, &pe->psi_buffer_used);
                        free(section);
                        continue;
                    }

                    handle_section(pid, section);
                }

                payload = ts_next_section(ts_packet);
                payload_length = TS_SIZE - (payload - ts_packet);
                /*
                 * There may be multiple sections in a single TS packet payload
                 * (pointer_field may point to the start of a following section).
                 * Loop until we've consumed the payload. Each completed `section`
                 * is validated and dispatched.
                 */
                while (payload_length)
                {
                    section = psi_assemble_payload(&pe->psi_buffer, &pe->psi_buffer_used,
                                                   (const uint8_t **)&payload, &payload_length);
                    if (section)
                    {
                        if (!psi_validate(section))
                        {
                            // Invalid PSI section, discard
                            psi_assemble_reset(&pe->psi_buffer, &pe->psi_buffer_used);
                            free(section);
                            break;
                        }

                        handle_section(pid, section);
                    }
                }
            }
        }

    stats_print:

        if (now - last_stats >= 10000000)
        {
            //[igmp://239.239.2.1:1234 UDP|0.2 s|SPTS$|1] OK bitrate: 0.00  (effective: 0.00 peak: 0.00) Mbps cc: 0 (data: 0) sync: 0 tei: 0
            double bitrate = (packets_all - last_packet_count) * TS_SIZE * 8 / ((now - last_stats) / 1000000.0);
            double data_bitrate = (packets_data - last_packets_data) * TS_SIZE * 8 / ((now - last_stats) / 1000000.0);
            if (!quiet_mode)
            {
                out_timestamp();
                printf(" [%s:%d|", multicast_addr, port);
                if (service_count() > 1)
                {
                    out_color(COLOR_CYAN);
                    printf("MPTS");
                    out_reset();
                    printf("%zu] ", service_count());
                }
                else if (service_count() == 1)
                {
                    out_color(COLOR_GREEN);
                    const char *name = service_get_name(1);
                    if (name)
                    {
                        fputs(name, stdout);
                    }
                    else
                    {
                        // FIXME: get service id
                        printf("unknown");
                    }
                    if (service_scrambled(0))
                    {
                        out_color(COLOR_RED);
                        printf("$");
                    }
                    out_reset();
                    printf("] ");
                }

                if (cc_errors > 100)
                {
                    out_color(COLOR_RED);
                    printf("CC");
                }
                else if (cc_errors > 10)
                {
                    out_color(COLOR_YELLOW);
                    printf("CC");
                }
                else if (tsusecs() - last_ts > 500000 || last_ts == 0)
                {
                    out_color(COLOR_RED);
                    printf("DEAD");
                }
                else
                {
                    out_color(COLOR_GREEN);
                    printf("OK");
                }
                out_reset();
                printf(" bitrate %.2f (data: %.2f) Mbps cc=",
                       bitrate / 1000000.0, data_bitrate / 1000000.0);
                out_number((out_number_t){
                    .value = cc_errors - last_cc_errors,
                    .format = Dec,
                    .warning = 10,
                    .critical = 100,
                });
                printf(" sync=");
                out_number((out_number_t){
                    .value = sync_errors - last_sync_errors,
                    .format = Dec,
                    .warning = 1,
                    .critical = 10,
                });
                printf(" tei=");
                out_number((out_number_t){
                    .value = tei_errors - last_tei_errors,
                    .format = Dec,
                    .warning = 1,
                    .critical = 10,
                });
                printf("\n");
            }

            if (log_file)
            {
                uint64_t timestamp = now / 1000000;
                fprintf(log_file, "%" PRIu64 ",%.2f,%.2f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
                        timestamp,
                        bitrate / 1000.0,
                        data_bitrate / 1000.0,
                        cc_errors,
                        sync_errors,
                        tei_errors,
                        packets_all - last_packet_count,
                        packets_data - last_packets_data);
                fflush(log_file);
            }

            last_stats = now;
            last_packet_count = packets_all;
            last_packets_data = packets_data;
            last_sync_errors = sync_errors;
            last_cc_errors = cc_errors;
            last_tei_errors = tei_errors;
        }
    }
    close(fd);
    if (log_file)
    {
        fclose(log_file);
    }

    // Print summary
    if (!quiet_mode)
    {
        uint64_t total_time = tsusecs() - start_ts;
        double total_bitrate = packets_all * TS_SIZE * 8 / (total_time / 1000000.0);
        double total_data_bitrate = packets_data * TS_SIZE * 8 / (total_time / 1000000.0);
        printf("Final stats:\n");
        printf("  total bitrate: %.2f Mbps\n", total_bitrate / 1000000.0);
        printf("  total data bitrate: %.2f Mbps\n", total_data_bitrate / 1000000.0);
        printf("  total packets: %" PRIu64 "\n", packets_all);
        printf("  sync errors: ");
        out_number((out_number_t){
            .value = sync_errors,
            .format = Dec,
            .warning = 1,
            .critical = 10,
        });
        printf("\n");
        printf("  cc errors: ");
        out_number((out_number_t){
            .value = cc_errors,
            .format = Dec,
            .warning = 10,
            .critical = 100,
        });
        printf("\n");
        printf("  tei errors: ");
        out_number((out_number_t){
            .value = tei_errors,
            .format = Dec,
            .warning = 1,
            .critical = 10,
        });
        printf("\n");
    }

    // Cleanup to make myself happy and valgrind quiet
    pat_cleanup();
    sdt_cleanup();
    service_free_all();

    return 0;
}