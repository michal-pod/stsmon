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
#include <bitstream/dvb/si/sdt.h>
#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si/desc_48.h>
#pragma GCC diagnostic pop
#include "pid.h"
#include "services.h"
#include "dvb.h"
#include "output.h"

/*
 * SDT section tables:
 * - `sdt_sections_next` accumulates incoming sections until a full table
 *   is available (managed via `psi_table_section`).
 * - When complete, `handle_sdt` swaps `sdt_sections_next` into
 *   `sdt_sections_current` and processes services. Keeping these as
 *   static makes them module-local and avoids exposing pointers.
 */
static PSI_TABLE_DECLARE(sdt_sections_current);
static PSI_TABLE_DECLARE(sdt_sections_next);

void sdt_cleanup()
{
    psi_table_free(sdt_sections_current);
    psi_table_free(sdt_sections_next);
}

void handle_sdt()
{
    PSI_TABLE_DECLARE(old_sections);
    uint8_t last_section = psi_table_get_lastsection(sdt_sections_next);
    uint8_t i;

    if (psi_table_validate(sdt_sections_current) &&
        psi_table_compare(sdt_sections_current, sdt_sections_next))
    {
        /* Identical SDT. Shortcut. */
        psi_table_free(sdt_sections_next);
        psi_table_init(sdt_sections_next);
        return;
    }

    if (!sdt_table_validate(sdt_sections_next))
    {
        out_log(LogLevel_Error, "Invalid SDT received");
        psi_table_free(sdt_sections_next);
        psi_table_init(sdt_sections_next);
        return;
    }

    /* Switch tables. */
    psi_table_copy(old_sections, sdt_sections_current);
    psi_table_copy(sdt_sections_current, sdt_sections_next);
    psi_table_init(sdt_sections_next);

    /* Log the update (version and last_section of the newly installed table). */
    out_log(LogLevel_Info, "SDT updated, version %u last_section %u", psi_table_get_version(sdt_sections_current), last_section);

    for (i = 0; i <= last_section; i++)
    {
        uint8_t *section = psi_table_get_section(sdt_sections_current, i);
        /*
         * Iterate services in the SDT section. `sdt_get_service` returns
         * a pointer to the service descriptor within the section buffer.
         * We must not free `section` here — the table owns it and will
         * be freed later by `psi_table_free` when appropriate.
         */
        uint8_t *service;
        int j = 0;
        while ((service = sdt_get_service(section, j)) != NULL)
        {
            uint16_t sid = sdtn_get_sid(service);
            bool scrambled = sdtn_get_ca(service);
            out_log(LogLevel_Info, "  Service SID: %u", sid);
            uint16_t k = 0;
            uint8_t *desc;
            while ((desc = descl_get_desc(sdtn_get_descs(service) + DESCS_HEADER_SIZE, descs_get_length(sdtn_get_descs(service)), k)) != NULL)
            {
                uint8_t tag = desc_get_tag(desc);
                
                if(tag == 0x48) // Service Descriptor
                {

                    uint8_t service_type = desc48_get_type(desc);
                    uint8_t provider_name_length;
                    uint8_t* provider_name = desc48_get_provider(desc, &provider_name_length);
                    

                    uint8_t service_name_length;
                    uint8_t* service_name = desc48_get_service(desc, &service_name_length);

                    /*
                     * Decode DVB-encoded strings into UTF-8. Returned buffers
                     * are heap allocated and must be freed after use.
                     */
                    char* provider_name_decoded = dvb_string_decode(provider_name, provider_name_length);
                    char* service_name_decoded = dvb_string_decode(service_name, service_name_length);

                    
                    out_log(LogLevel_Info, "    Service Descriptor:");
                    out_log(LogLevel_Info, "      Service Type: 0x%02X", service_type);
                    out_log(LogLevel_Info, "      Provider Name: %s", provider_name_decoded);
                    out_log(LogLevel_Info, "      Service Name: %s", service_name_decoded);

                    /* Register or update the service name and scrambled flag.
                     * PMT PID is unknown here (0) so it will be set later by PAT processing.
                     */
                    service_update(sid, (const char*)service_name_decoded, 0, scrambled);
                    free(provider_name_decoded);
                    free(service_name_decoded);
                }
                k++;
            }

            j++;
        }
        (void)section; /* section ownership remains with the table */
    }

    if (psi_table_validate(old_sections))
        psi_table_free(old_sections);
}

void handle_sdt_section(uint16_t pid, uint8_t *section)
{
    if (pid != SDT_PID || !sdt_validate(section))
    {
        out_log(LogLevel_Error, "Invalid SDT section on PID %u", pid);
        free(section);
        return;
    }

    if (!psi_table_section(sdt_sections_next, section))
    {
        free(section);
        return;
    }

    handle_sdt();
}