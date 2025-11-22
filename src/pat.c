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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#include <bitstream/mpeg/psi.h>
#include <bitstream/mpeg/psi/pat.h>
#include <bitstream/dvb/si/nit.h>
#pragma GCC diagnostic pop

#include "pid.h"
#include "services.h"
#include "output.h"

/*
 * PAT section storage: next/current model similar to SDT.
 * `pat_sections_next` collects incoming sections; when a full table is
 * available `handle_pat` swaps it into `pat_sections_current`.
 */
static PSI_TABLE_DECLARE(pat_sections_current);
static PSI_TABLE_DECLARE(pat_sections_next);

void pat_cleanup()
{
    psi_table_free(pat_sections_current);
    psi_table_free(pat_sections_next);
}

void handle_pat()
{
    PSI_TABLE_DECLARE(old_sections);
    uint8_t last_section = psi_table_get_lastsection(pat_sections_next);
    uint8_t i;

    if (psi_table_validate(pat_sections_current) &&
        psi_table_compare(pat_sections_current, pat_sections_next))
    {
        /* Identical PAT. Shortcut. */
        psi_table_free(pat_sections_next);
        psi_table_init(pat_sections_next);
        return;
    }

    if (!pat_table_validate(pat_sections_next))
    {
        out_log(LogLevel_Error, "Invalid PAT received");
        psi_table_free(pat_sections_next);
        psi_table_init(pat_sections_next);
        return;
    }

    /* Switch tables. */
    psi_table_copy(old_sections, pat_sections_current);
    psi_table_copy(pat_sections_current, pat_sections_next);
    psi_table_init(pat_sections_next);

    for (i = 0; i <= last_section; i++)
    {
        uint8_t *section = psi_table_get_section(pat_sections_current, i);
        const uint8_t *program;
        int j = 0;

        while ((program = pat_get_program(section, j)) != NULL)
        {
            const uint8_t *old_program = NULL;
            uint16_t sid = patn_get_program(program);
            uint16_t pid = patn_get_pid(program);
            j++;

            if (sid == 0)
            {
                if (pid != NIT_PID)
                    out_log(LogLevel_Warning,
                            "NIT is carried on PID %hu which isn't DVB compliant",
                            pid);
                continue; /* NIT */
            }

            /* If the program didn't exist previously we consider it new and
             * mark its PID as carrying PSI (so PMT sections will be collected).
             */
            if (!psi_table_validate(old_sections) || (old_program =
                                                          pat_table_find_program(old_sections, sid)) == NULL)
            {
                out_log(LogLevel_Info, "New program found: SID %hu on PID %hu", sid, pid);
                pid_table[pid].is_psi = true;
                service_set_pmt_pid(sid, pid);
            }
            else
            {
                uint16_t old_pid = patn_get_pid(old_program);
                  if (old_pid != pid)
                {
                    out_log(LogLevel_Info, "Program SID %hu changed PID from %hu to %hu", sid, old_pid, pid);
                    pid_table[pid].is_psi = true;
                    ts_pid_t *old_pid_entry = &pid_table[old_pid];
                    old_pid_entry->is_psi = false;
                    service_set_pmt_pid(sid, pid);
                    psi_assemble_reset(&old_pid_entry->psi_buffer,
                                       &old_pid_entry->psi_buffer_used);
                }
            }
        }
    }

    if (psi_table_validate(old_sections))
        psi_table_free(old_sections);
}

void handle_pat_section(uint16_t pid, uint8_t *section)
{
    if (pid != PAT_PID || !pat_validate(section))
    {
        out_log(LogLevel_Error, "Invalid PAT section on PID %u", pid);
        free(section);
        return;
    }

    if (!psi_table_section(pat_sections_next, section))
    {
        free(section);
        return;
    }

    handle_pat();
}