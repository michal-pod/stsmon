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
#pragma once
#include <stdint.h>
#include <stdbool.h>

#include <bitstream/mpeg/psi.h>
#define TS_MAX_PID 8192
typedef struct ts_pid
{
    uint8_t last_cc;
    uint64_t packets;
    bool is_psi;
    bool is_data;
    uint8_t *psi_buffer;
    uint16_t psi_buffer_used;
} ts_pid_t;

extern ts_pid_t pid_table[TS_MAX_PID];