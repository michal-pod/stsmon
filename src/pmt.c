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
#include <bitstream/mpeg/psi/pmt.h>
#pragma GCC diagnostic pop
#include "pid.h"

#include "services.h"
#include "output.h"

void handle_pmt(uint16_t pid, uint8_t *section)
{
    if (!pmt_validate(section))
    {
        out_log(LogLevel_Error, "Invalid PMT section on PID %u", pid);
        free(section);
        return;
    }
    uint16_t service_id = pmt_get_program(section);
    uint8_t last_pmt_version = service_get_pmt_version(service_id);
    uint8_t current_pmt_version = psi_get_version(section);
    if (current_pmt_version != last_pmt_version)
    {
        service_set_pmt_version(service_id, current_pmt_version);
        out_log(LogLevel_Info, "PMT version change for service ID %u: %u -> %u",
                service_id, last_pmt_version, current_pmt_version);
        uint8_t *es;
        int i = 0;
        while ((es = pmt_get_es(section, i)) != NULL)
        {
            uint8_t es_type = pmtn_get_streamtype(es);
            uint16_t es_pid = pmtn_get_pid(es);
            bool has_data = false;
            switch (es_type)
            {
            case PMT_STREAMTYPE_VIDEO_MPEG1:
            case PMT_STREAMTYPE_VIDEO_MPEG2:
            case PMT_STREAMTYPE_VIDEO_MPEG4:
            case PMT_STREAMTYPE_VIDEO_AVC:
            case PMT_STREAMTYPE_VIDEO_HEVC:
            case PMT_STREAMTYPE_AUDIO_MPEG1:
            case PMT_STREAMTYPE_AUDIO_MPEG2:
            case PMT_STREAMTYPE_AUDIO_ADTS:
                has_data = true;
            default:
                ;
            }
            uint8_t* desc = pmtn_get_descs(es);
            uint16_t desc_length = pmtn_get_desclength(es) + DESCS_HEADER_SIZE;

            uint8_t *es_desc;
            int j = 0;
            while ((es_desc = descl_get_desc(desc, desc_length, j)) != NULL)
            {
                uint8_t tag = desc_get_tag(es_desc);
                switch(tag)
                {
                    case 0x6a: // AC-3 descriptor
                    case 0x7a: // Enhanced AC-3 descriptor
                    case 0x7f: // DTS descriptor
                        has_data = true;
                    default:
                        ;
                }
                j++;
            }
                /* Mark PID as data-bearing if stream type or descriptors indicate so.
                 * This influences statistics/monitoring and can be used to ignore
                 * purely signalling streams.
                 */
                pid_table[es_pid].is_data = has_data;

                out_log(LogLevel_Info, "  ES PID: %u, Stream Type: 0x%02X Data: %s",
                    es_pid, es_type, has_data ? "Yes" : "No");
            i++;
        }
    }
    free(section);
}
