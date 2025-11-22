% stsmon(1) tsmon manual page
% Michał Podsiadlik
% 2025-11-25

# NAME

tsmon - a simple DVB transport stream monitor

# SYNOPSIS

stsmon -m *multicast-addr* [options]

# DESCRIPTION

`stsmon` monitors a DVB transport stream received from an IP multicast group. It receives MPEG-TS packets, validates packet sync and continuity counters, assembles PSI/SI sections (PAT/PMT/SDT) and prints concise status information to the console. Optionally the tool can log periodic CSV statistics to a file.

The program runs until it receives a termination signal (SIGINT or SIGTERM).

# OPTIONS

-m *group*, --multicast *group*
: Set multicast address (required)

-p *port*, --port *port*
: Set UDP port number (default: 1234)

-i *address*, --interface *address*
: Set local interface address, this is required on Windows platforms to select the correct network interface for multicast reception. On other platforms, if not provided, INADDR_ANY is used and interface selection is done by the OS.

-c, --show-cc
: Show continuity counter (CC) change details

-t, --show-times
: Show packet timing information, including inter-arrival times. This will produce a lot of output.

-l, --csv *file*
: Append periodic statistics to CSV *file*, creating it with a header if it does not exist. Format of the CSV is described in the OUTPUT section.

-q, --quiet
: Quiet mode reduces console output, single `-q` suppresses informational messages, double `-qq` suppresses all output except errors.

-h, --help
: Show help and exit

-v, --version
: Show version and license information and exit


# OUTPUT

By default `stsmon` prints a compact status line periodically that includes bitrate, CC errors, sync errors and TEI errors. When `--show-cc` or `--show-times` are enabled, more verbose per-packet diagnostics are printed.

When `--csv *file*` is used the tool appends a CSV header and periodic rows with the following columns:

- `Timestamp` (unix seconds)
- `Bitrate (kbps)`
- `Data Bitrate (kbps)`
- `CC Errors`
- `Sync Errors`
- `TEI Errors`
- `Total Packets`
- `Data Packets`

# EXIT STATUS

The program returns 0 on normal termination (signal or exit), and non-zero on error (for example when socket setup or multicast join fail).

# EXAMPLES

Start monitoring multicast group 239.239.2.1 on default port 1234:

```
stsmon -m 239.239.2.1
```

Start monitoring and log CSV statistics to `/var/log/tsmon.csv` while suppressing most console output:

```
stsmon -m 239.239.2.1 -l /var/log/tsmon.csv -q
```

Show continuity counter details and packet timing information:

```
stsmon -m 239.239.2.1 -c -t
```

# FILES

- CSV log file: whatever path provided with `--csv` is appended to by the program.

# AUTHOR

Michał Podsiadlik

# COPYRIGHT

Copyright (C) 2025 Michał Podsiadlik.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 3 of the License, or (at your option) any later version.
