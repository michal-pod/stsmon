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
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <locale.h>

int show_cc = 0;
int show_times = 0;
int quiet_mode = 0;
const char *csv_file = NULL;

extern int monitor_stream(const char *multicast_addr, uint16_t port, const char *local_interface);

int main(int argc, char **argv)
{
    char *multicast_addr = NULL;
    uint16_t port = 1234;

    static struct option long_options[] = {
        {"multicast", required_argument, 0, 'm'},
        {"interface", required_argument, 0, 'i'},
        {"port", required_argument, 0, 'p'},
        {"show-cc", no_argument, 0, 'c'},
        {"show-times", no_argument, 0, 't'},
        {"csv", required_argument, 0, 'l'},
        {"quiet", no_argument, 0, 'q'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}};

    int opt;
    char *local_interface = NULL;
    while ((opt = getopt_long(argc, argv, "m:i:p:ctql:hv", long_options, NULL)) != -1)
    {
        switch (opt)
        {
        case 'm':
            multicast_addr = optarg;
            break;
        case 'i':
            local_interface = optarg;
            break;
        case 'l':
            csv_file = optarg;
            break;
        case 'q':
            quiet_mode = 1;
            break;
        case 'p':
            port = (uint16_t)atoi(optarg);
            break;
        case 'c':
            show_cc = 1;
            break;
        case 't':
            show_times = 1;
            break;
        case 'h':
            printf("Usage: stsmon [options]\n");
            printf("Options:\n");
            printf("  -m, --multicast <address>   Set multicast address\n");
            printf("  -p, --port <port>           Set port number\n");
            printf("  -i, --interface <address>   Set local interface address (required on Windows)\n");
            printf("  -c, --show-cc               Show congestion control info\n");
            printf("  -t, --show-times            Show timing information\n");
            printf("  -l, --csv <file>            Log data to CSV file\n");
            printf("  -q, --quiet                 Quiet mode reduces console output (use -qq to disable completely)\n");
            printf("  -h, --help                  Show this help message\n");
            printf("  -v, --version               Show version information\n");
            return 0;
        case 'v':
            printf("stsmon version " VERSION "\n");
            printf("Copyright (C) 2025 Michał Podsiadlik\n");
            printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
            printf("This is free software: you are free to change and redistribute it.\n");
            printf("There is NO WARRANTY, to the extent permitted by law.\n");

            return 0;
        default:
            fprintf(stderr, "Unknown option. Use -h for help.\n");
            return 1;
        }
    }

    if (!multicast_addr)
    {
        fprintf(stderr, "Multicast address is required. Use -h for help.\n");
        return 1;
    }
    #ifdef WIN32
    if(!local_interface)
    {
        fprintf(stderr, "On Windows, local interface address is required. Use -h for help.\n");
        return 1;
    }
    #endif

    if (quiet_mode > 1 && csv_file == NULL)
    {
        fprintf(stderr, "Console output is disabled and not log file specified.\n");        
        fprintf(stderr, "Will not report any data.\n");
    }

    // Ensure consistent locale for number formatting
    setlocale(LC_ALL, "C");

    /*
     * Start monitoring: `monitor_stream` performs socket setup and an
     * event loop that collects TS packets, performs PSI assembly and
     * dispatches tables. The process runs until signalled (SIGINT/SIGTERM).
     */
    return monitor_stream(multicast_addr, port, local_interface);
}