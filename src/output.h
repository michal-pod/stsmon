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

enum ConsoleColors {
    COLOR_RESET = 0,
    COLOR_RED = 31,
    COLOR_GREEN = 32,
    COLOR_YELLOW = 33,
    COLOR_BLUE = 34,
    COLOR_MAGENTA = 35,
    COLOR_CYAN = 36,
    COLOR_WHITE = 37
};
void out_timestamp();
void out_color(enum ConsoleColors color);
void out_reset();
void out_flush();
typedef struct {
    uint64_t value;
    double value_f;
    uint64_t warning;
    uint64_t critical;
    enum {
        Hex,
        Dec
    } format;
    int precision;
} out_number_t;

void out_number(out_number_t);

typedef enum {
    LogLevel_Info,
    LogLevel_Warning,
    LogLevel_Error
} OutLogLevel;

void out_log(OutLogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));