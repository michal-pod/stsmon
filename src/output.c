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
#include "output.h"
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <inttypes.h>
#ifdef WIN32
#include <windows.h>
#endif

extern int quiet_mode;
void out_timestamp()
{
    time_t t = time(NULL);

    struct tm *tmp = localtime(&t);
    if (tmp == NULL)
    {
        return;
    }
    char buf[64];
    if (strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmp) == 0)
    {
        return;
    }
    out_color(COLOR_WHITE);
    fputs(buf, stdout);
    out_reset();
}

void out_color(enum ConsoleColors color)
{
    #ifdef WIN32
    // Windows CMD does not support ANSI escape codes by default
    // Consider using SetConsoleTextAttribute for better compatibility
    WORD wColor;
    switch(color) {
        case COLOR_RED: wColor = FOREGROUND_RED; break;
        case COLOR_GREEN: wColor = FOREGROUND_GREEN; break;
        case COLOR_YELLOW: wColor = FOREGROUND_RED | FOREGROUND_GREEN; break;
        case COLOR_BLUE: wColor = FOREGROUND_BLUE; break;
        case COLOR_MAGENTA: wColor = FOREGROUND_RED | FOREGROUND_BLUE; break;
        case COLOR_CYAN: wColor = FOREGROUND_GREEN | FOREGROUND_BLUE; break;
        case COLOR_WHITE: wColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break;
        case COLOR_RESET: default: wColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break;
    }
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), wColor);
    #else
    printf("\e[%dm", color);
    #endif
}

void out_reset()
{
    out_color(COLOR_RESET);    
}

void out_flush()
{
    fflush(stdout);
}

void out_number(out_number_t num)
{
    if (num.critical != 0 && num.value >= num.critical)
    {
        out_color(COLOR_RED);           
    }
    else if (num.warning != 0 && num.value >= num.warning)
    {
        out_color(COLOR_YELLOW);
    }
    else
    {
        out_color(COLOR_GREEN);
    }
    if (num.format == Hex)
    {
        printf("0x%" PRIx64, (uint64_t)num.value);
    }
    else
    {
        if (num.precision > 0)
        {
            printf("%.*f", num.precision, num.value_f);
        }
        else
        {
            printf("%" PRIu64, (uint64_t)num.value);
        }
    }

    if (num.critical != 0 || num.warning != 0)
        out_reset();
}

void out_log(OutLogLevel level, const char* fmt, ...)
{
    if(quiet_mode && level == LogLevel_Info)
        return;
    
    if(quiet_mode > 1)
        return;

    va_list args;
    va_start(args, fmt);

    out_timestamp();
    printf(" ");

    switch (level)
    {
        case LogLevel_Info:
            out_color(COLOR_GREEN);
            fputs("Info: ", stdout);
            break;
        case LogLevel_Warning:
            out_color(COLOR_YELLOW);
            fputs("Warning: ", stdout);
            break;
        case LogLevel_Error:
            out_color(COLOR_RED);
            fputs("Error: ", stdout);
            break;
    }

    vprintf(fmt, args);
    out_reset();
    printf("\n");

    va_end(args);
}