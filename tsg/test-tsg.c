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
 * 
 * transport-stream-generator.c - Generate test MPEG-TS with intentional CC errors
 *
 * This program generates a transport stream with:
 * - PAT, PMT, and SDT tables
 * - MPEG2 Video, MPEG2 Audio, and DVB Subtitles
 * - Intentional continuity counter errors on video PID every 15 seconds
 * - Output to UDP multicast 239.239.42.12:1234
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>
#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <signal.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/psi/pat.h>
#include <bitstream/mpeg/psi/pmt.h>
#include <bitstream/dvb/si/sdt.h>
#include <bitstream/dvb/si/desc_48.h>
#pragma GCC diagnostic pop

/* Configuration */
#define MCAST_ADDR "239.239.42.12"
#define MCAST_PORT 1234
#define BITRATE 3800000 /* ~3.8 Mbps - typical SD bitrate */
#define TS_PACKET_SIZE 188
#define PACKETS_PER_SEC (BITRATE / 8 / TS_PACKET_SIZE)

/* PIDs */
#define PID_PAT 0x0000
#define PID_SDT 0x0011
#define PID_PMT 0x0100
#define PID_VIDEO 0x0101
#define PID_AUDIO 0x0102
#define PID_SUBTITLES 0x0103
#define PID_PCR PID_VIDEO

/* Program details */
#define TSID 1
#define SID 1
#define ONID 1
#define PMT_PID PID_PMT

/* Continuity counters */
static uint8_t cc_pat = 0;
static uint8_t cc_pmt = 0;
static uint8_t cc_sdt = 0;
static uint8_t cc_video = 0;
static uint8_t cc_audio = 0;
static uint8_t cc_subtitles = 0;

/* Timing */
static time_t start_time;
static time_t last_cc_error_time;
static uint64_t packet_count = 0;

/* Socket */
static int sockfd = -1;
static struct sockaddr_in dest_addr;

/* Batch buffer: send 7 TS packets per UDP datagram */
#define TS_PER_UDP 7
static uint8_t udp_buf[TS_PER_UDP * TS_PACKET_SIZE];
static int udp_buf_count = 0;

/* Initialize UDP socket */
static int init_socket(void)
{
    #ifdef WIN32
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", iResult);
        return -1;
    }
    #endif
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return -1;
    }

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(MCAST_PORT);
    dest_addr.sin_addr.s_addr = inet_addr(MCAST_ADDR);

    if (dest_addr.sin_addr.s_addr == INADDR_NONE)
    {
        perror("inet_addr");
        close(sockfd);
        return -1;
    }

    printf("Sending to %s:%d\n", MCAST_ADDR, MCAST_PORT);
    return 0;
}

/* Flush UDP buffer (send accumulated TS packets as one UDP datagram) */
static int flush_udp_buf(void)
{
    if (udp_buf_count == 0)
        return 0;

    ssize_t to_send = udp_buf_count * TS_PACKET_SIZE;
    ssize_t sent = sendto(sockfd, (const char *)udp_buf, to_send, 0,
                          (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (sent < 0)
    {
        perror("sendto");
        return -1;
    }
    packet_count += udp_buf_count;
    udp_buf_count = 0;
    return 0;
}

static void handle_sigint(int signo)
{
    (void)signo;
    printf("\nCaught signal, flushing buffer and exiting...\n");
    flush_udp_buf();
    if (sockfd >= 0)
        close(sockfd);
    exit(0);
}

/* Enqueue a TS packet to UDP buffer; flush when full */
static int send_ts_packet(const uint8_t *packet)
{
    /* copy packet into buffer */
    memcpy(udp_buf + udp_buf_count * TS_PACKET_SIZE, packet, TS_PACKET_SIZE);
    udp_buf_count++;
    if (udp_buf_count >= TS_PER_UDP)
    {
        return flush_udp_buf();
    }
    return 0;
}

/* Generate PAT (Program Association Table) */
static void generate_pat(void)
{
    uint8_t *pat = psi_allocate();
    uint8_t *pat_n;

    pat_init(pat);
    psi_set_version(pat, 0);
    psi_set_current(pat);
    pat_set_tsid(pat, TSID);
    psi_set_section(pat, 0);
    psi_set_lastsection(pat, 0);
    pat_set_length(pat, PSI_MAX_SIZE);

    /* NIT PID */
    pat_n = pat_get_program(pat, 0);
    patn_init(pat_n);
    patn_set_program(pat_n, 0);
    patn_set_pid(pat_n, 0x10); /* NIT PID */

    /* PMT for our service */
    pat_n = pat_get_program(pat, 1);
    patn_init(pat_n);
    patn_set_program(pat_n, SID);
    patn_set_pid(pat_n, PMT_PID);

    /* Calculate actual length */
    pat_n = pat_get_program(pat, 2);
    pat_set_length(pat, pat_n - pat - PAT_HEADER_SIZE);
    psi_set_crc(pat);

    /* Output PAT in TS packets */
    uint16_t section_length = psi_get_length(pat) + PSI_HEADER_SIZE;
    uint16_t section_offset = 0;

    do
    {
        uint8_t ts[TS_SIZE];
        uint8_t ts_offset = 0;
        memset(ts, 0xff, TS_SIZE);

        psi_split_section(ts, &ts_offset, pat, &section_offset);
        ts_set_pid(ts, PID_PAT);
        ts_set_cc(ts, cc_pat);
        cc_pat = (cc_pat + 1) & 0xf;

        if (section_offset == section_length)
            psi_split_end(ts, &ts_offset);

        send_ts_packet(ts);
    } while (section_offset < section_length);

    free(pat);
}

/* Generate PMT (Program Map Table) */
static void generate_pmt(void)
{
    uint8_t *pmt = psi_allocate();
    uint8_t *pmt_n;

    pmt_init(pmt);
    psi_set_version(pmt, 0);
    psi_set_current(pmt);
    pmt_set_program(pmt, SID);
    pmt_set_pcrpid(pmt, PID_PCR);
    pmt_set_desclength(pmt, 0);
    pmt_set_length(pmt, PSI_MAX_SIZE);

    /* MPEG2 Video */
    pmt_n = pmt_get_es(pmt, 0);
    pmtn_init(pmt_n);
    pmtn_set_streamtype(pmt_n, 0x02); /* MPEG2 Video */
    pmtn_set_pid(pmt_n, PID_VIDEO);
    pmtn_set_desclength(pmt_n, 0);

    /* MPEG2 Audio */
    pmt_n = pmt_get_es(pmt, 1);
    pmtn_init(pmt_n);
    pmtn_set_streamtype(pmt_n, 0x04); /* MPEG2 Audio */
    pmtn_set_pid(pmt_n, PID_AUDIO);
    pmtn_set_desclength(pmt_n, 0);

    /* DVB Subtitles */
    pmt_n = pmt_get_es(pmt, 2);
    pmtn_init(pmt_n);
    pmtn_set_streamtype(pmt_n, 0x06); /* Private PES (for subtitles) */
    pmtn_set_pid(pmt_n, PID_SUBTITLES);
    pmtn_set_desclength(pmt_n, 0);

    /* Calculate actual length */
    pmt_n = pmt_get_es(pmt, 3);
    pmt_set_length(pmt, pmt_n - pmt - PMT_HEADER_SIZE);
    psi_set_crc(pmt);

    /* Output PMT in TS packets */
    uint16_t section_length = psi_get_length(pmt) + PSI_HEADER_SIZE;
    uint16_t section_offset = 0;

    do
    {
        uint8_t ts[TS_SIZE];
        uint8_t ts_offset = 0;
        memset(ts, 0xff, TS_SIZE);

        psi_split_section(ts, &ts_offset, pmt, &section_offset);
        ts_set_pid(ts, PMT_PID);
        ts_set_cc(ts, cc_pmt);
        cc_pmt = (cc_pmt + 1) & 0xf;

        if (section_offset == section_length)
            psi_split_end(ts, &ts_offset);

        send_ts_packet(ts);
    } while (section_offset < section_length);

    free(pmt);
}

/* Generate SDT (Service Description Table) */
static void generate_sdt(void)
{
    uint8_t *sdt = psi_allocate();
    uint8_t *sdt_n;
    uint8_t *desc_loop, *desc;

    sdt_init(sdt, true); /* actual SDT */
    psi_set_version(sdt, 0);
    psi_set_current(sdt);
    sdt_set_tsid(sdt, TSID);
    sdt_set_onid(sdt, ONID);
    psi_set_section(sdt, 0);
    psi_set_lastsection(sdt, 0);
    sdt_set_length(sdt, PSI_MAX_SIZE);

    /* Service description */
    sdt_n = sdt_get_service(sdt, 0);
    sdtn_init(sdt_n);
    sdtn_set_sid(sdt_n, SID);
    sdtn_set_eitschedule(sdt_n);
    sdtn_set_eitpresent(sdt_n);
    sdtn_set_running(sdt_n, 4); /* running */
    sdtn_set_ca(sdt_n);         /* free access */

    /* Add service descriptor (0x48) */
    desc_loop = sdtn_get_descs(sdt_n);
    descs_set_length(desc_loop, DESCS_MAX_SIZE);

    desc = descs_get_desc(desc_loop, 0);
    desc48_init(desc);
    desc48_set_type(desc, 0x01); /* digital television service */
    const char* provider_name = "Test";    
    desc48_set_provider(desc, (uint8_t *)provider_name, strlen(provider_name));
    const char* service_name = "\x15Żółty🟡";
    desc48_set_service(desc, (uint8_t *)service_name, strlen(service_name));
    
    desc48_set_length(desc);

    /* Finalize descriptors */
    desc = descs_get_desc(desc_loop, 1);
    descs_set_length(desc_loop, desc - desc_loop - DESCS_HEADER_SIZE);

    /* Calculate actual length */
    sdt_n = sdt_get_service(sdt, 1);
    sdt_set_length(sdt, sdt_n - sdt - SDT_HEADER_SIZE);
    psi_set_crc(sdt);

    /* Output SDT in TS packets */
    uint16_t section_length = psi_get_length(sdt) + PSI_HEADER_SIZE;
    uint16_t section_offset = 0;

    do
    {
        uint8_t ts[TS_SIZE];
        uint8_t ts_offset = 0;
        memset(ts, 0xff, TS_SIZE);

        psi_split_section(ts, &ts_offset, sdt, &section_offset);
        ts_set_pid(ts, PID_SDT);
        ts_set_cc(ts, cc_sdt);
        cc_sdt = (cc_sdt + 1) & 0xf;

        if (section_offset == section_length)
            psi_split_end(ts, &ts_offset);

        send_ts_packet(ts);
    } while (section_offset < section_length);

    free(sdt);
}

/* Generate a null/stuffing TS packet for a given PID */
static void generate_es_packet(uint16_t pid, uint8_t *cc, bool inject_cc_error)
{
    uint8_t ts[TS_SIZE];

    ts_init(ts);
    ts_set_pid(ts, pid);
    ts_set_payload(ts);

    if (inject_cc_error)
    {
        /* Skip CC to create error */
        *cc = (*cc + 2) & 0xf;
        printf("Injecting CC error on PID 0x%04x at packet %" PRIu64 " (time: %" PRId64 "s)\n",
               pid, packet_count, (int64_t)(time(NULL) - start_time));
    }

    ts_set_cc(ts, *cc);
    *cc = (*cc + 1) & 0xf;

    /* Fill payload with zeros */
    memset(ts + TS_HEADER_SIZE, 0, TS_SIZE - TS_HEADER_SIZE);

    send_ts_packet(ts);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("MPEG-TS Generator\n");
    printf("=================\n");
    printf("Bitrate: %.2f Mbps\n", BITRATE / 1000000.0);
    printf("Packets/sec: %d\n", PACKETS_PER_SEC);
    printf("\n");

    /* Initialize socket */
    if (init_socket() < 0)
        return 1;

    /* Install signal handler to flush buffer on exit */
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    /* Initialize timing */
    start_time = time(NULL);
    last_cc_error_time = start_time;

    /* Send initial PSI/SI tables */
    printf("Sending initial tables...\n");
    generate_pat();
    generate_pmt();
    generate_sdt();

    printf("\nStreaming started. Press Ctrl+C to stop.\n\n");

    /* Main streaming loop */
    time_t last_psi_time = start_time;
    time_t next_errors_inject_time = start_time + 15;

    while (1)
    {
        time_t now = time(NULL);

        /* Send PSI/SI tables every second */
        if (now > last_psi_time)
        {
            generate_pat();
            generate_pmt();
            generate_sdt();
            last_psi_time = now;
        }

        if(now >= next_errors_inject_time)
        {
            for(int i = 0; i < rand()%10; i++)
                generate_es_packet(PID_VIDEO, &cc_video, true);

            next_errors_inject_time += 15;
        }

        /* Generate ES packets with distribution:
         * Video: ~90% (higher bitrate)
         * Audio: ~8%
         * Subtitles: ~2%
         */
        int video_packets = 90;
        int audio_packets = 8;
        int subtitle_packets = 2;
        for (int i = 0; i < 100; i++)
        {
            int type = i % 3;
            if (type == 0 && video_packets > 0)
            {
                /* Video packets */
                generate_es_packet(PID_VIDEO, &cc_video, 0);
            }
            else if (type == 1 && audio_packets > 0)
            {
                /* Audio packets */
                generate_es_packet(PID_AUDIO, &cc_audio, false);
            }
            else if (type == 2 && subtitle_packets > 0)
            {
                /* Subtitle packet */
                generate_es_packet(PID_SUBTITLES, &cc_subtitles, false);
            }

            if (i % 7 == 0)
            {
                // 2500 us sleep to pace the stream
                usleep(2500);
            }
        }
    }

    close(sockfd);
    return 0;
}
