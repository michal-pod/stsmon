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
#include <stdlib.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#include <bitstream/mpeg/psi.h>
#pragma GCC diagnostic pop

typedef struct service_psi_buffer_t
{
    uint16_t service_id;
    PSI_TABLE_DECLARE(current);
    PSI_TABLE_DECLARE(next);
} service_psi_buffer_t;

void service_update(uint16_t service_id, const char* name, uint16_t pmt_pid, bool scrambled);

uint16_t service_get_pmt_pid(uint16_t service_id);
void service_set_pmt_pid(uint16_t service_id, uint16_t pmt_pid);

const char* service_get_name(uint16_t service_id);
void service_set_name(uint16_t service_id, const char* name);

bool service_scrambled(uint16_t service_id);
void service_set_scrambled(uint16_t service_id, bool scrambled);

uint8_t service_get_pmt_version(uint16_t service_id);
void service_set_pmt_version(uint16_t service_id, uint8_t version);

size_t service_count();

void service_free(uint16_t service_id);
void service_free_all();