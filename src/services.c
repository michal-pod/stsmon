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
#include "services.h"
#include <string.h>
#include <stdlib.h>
#include "output.h"

typedef struct service_entry_t
{
    uint16_t service_id;
    uint16_t pmt_pid;
    char *name;
    bool scrambled;
    uint8_t pmt_version;
    struct service_entry_t *next;
} service_entry_t;

/*
 * Simple singly-linked list to hold discovered services.
 * The list head is `service_list`. New entries are pushed to the head
 * for simplicity (O(1) insert). This is sufficient for a small number
 * of services typical in monitoring tools.
 */
static service_entry_t *service_list = NULL;

static service_entry_t *_service_get(uint16_t service_id)
{
    if(service_id == 0)
        return service_list;

    service_entry_t *se = service_list;
    while (se)
    {
        if (se->service_id == service_id)
        {
            return se;
        }
        se = se->next;
    }
    return NULL;
}

static service_entry_t *_service_get_or_create(uint16_t service_id)
{
    service_entry_t *se = _service_get(service_id);
    if (se)
    {
        return se;
    }

    /* Create new entry and insert at head. Initial `pmt_version` of 0xff
     * indicates 'unknown' so the first PMT will always be considered a change.
     */
    service_entry_t *new_se = malloc(sizeof(service_entry_t));
    if(new_se == NULL)
    {
        out_log(LogLevel_Error, "Failed to allocate memory for new service entry");
        abort();
    }
    memset(new_se, 0, sizeof(service_entry_t));
    new_se->service_id = service_id;
    new_se->pmt_version = 0xff;
    new_se->next = service_list;
    service_list = new_se;
    return new_se;
}

void service_update(uint16_t service_id, const char* name, uint16_t pmt_pid, bool scrambled)
{
    service_entry_t *se = _service_get_or_create(service_id);
    if (name)
    {
        if (se->name)
        {
            free(se->name);
        }
        se->name = strdup(name);
        if(se->name == NULL)
        {
            out_log(LogLevel_Error, "Failed to allocate memory for service name");
            abort();
        }
    }
    se->pmt_pid = pmt_pid;
    se->scrambled = scrambled;
}

uint16_t service_get_pmt_pid(uint16_t service_id)
{
    service_entry_t *se = _service_get(service_id);
    if (!se)
    {
        return 0;
    }
    return se->pmt_pid;
}

void service_set_pmt_pid(uint16_t service_id, uint16_t pmt_pid)
{
    service_entry_t *se = _service_get_or_create(service_id);
    se->pmt_pid = pmt_pid;
}

const char* service_get_name(uint16_t service_id)
{
    service_entry_t *se = _service_get(service_id);
    if (!se)
    {
        return NULL;
    }
    return se->name ? se->name : "";
}

void service_set_name(uint16_t service_id, const char* name)
{
    service_entry_t *se = _service_get_or_create(service_id);
    if (se->name)
    {
        free(se->name);
    }
    se->name = strdup(name);
    if(se->name == NULL)
    {
        out_log(LogLevel_Error, "Failed to allocate memory for service name");
        abort();
    }
}

bool service_scrambled(uint16_t service_id)
{
    service_entry_t *se = _service_get(service_id);
    if (!se)
    {
        return false;
    }
    return se->scrambled;
}

void service_set_scrambled(uint16_t service_id, bool scrambled)
{
    service_entry_t *se = _service_get_or_create(service_id);
    se->scrambled = scrambled;
}

uint8_t service_get_pmt_version(uint16_t service_id)
{
    service_entry_t *se = _service_get(service_id);
    if (!se)
    {
        return 0xff;
    }
    return se->pmt_version;
}

void service_set_pmt_version(uint16_t service_id, uint8_t version)
{
    service_entry_t *se = _service_get_or_create(service_id);
    se->pmt_version = version;
}

size_t service_count()
{
    size_t count = 0;
    service_entry_t *se = service_list;
    while (se)
    {
        count++;
        se = se->next;
    }
    return count;
}

void service_free(uint16_t service_id)
{
    service_entry_t **se_ptr = &service_list;
    while (*se_ptr)
    {
        if ((*se_ptr)->service_id == service_id)
        {
            service_entry_t *to_free = *se_ptr;
            *se_ptr = (*se_ptr)->next;
            if (to_free->name)
            {
                free(to_free->name);
            }
            free(to_free);
            return;
        }
        se_ptr = &((*se_ptr)->next);
    }
}

void service_free_all()
{
    service_entry_t *se = service_list;
    while (se)
    {
        service_entry_t *to_free = se;
        se = se->next;
        if (to_free->name)
        {
            free(to_free->name);
        }
        free(to_free);
    }
    service_list = NULL;
}