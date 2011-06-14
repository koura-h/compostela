/* $Id$ */
#include <yaml.h>

#include <stdlib.h>
#include <stdio.h>

#include "appconfig.h"
#include "config.h"

#include "azlist.h"
#include "azlog.h"

char* g_config_server_address = NULL;
int   g_config_server_port = 0;
int   g_config_waiting_seconds = 1;
char* g_config_control_path = NULL;

az_list* g_config_patterns = NULL;

sc_config_pattern_entry*
_pick_pattern_entry(yaml_parser_t* parser)
{
    sc_config_pattern_entry* ret =
    (sc_config_pattern_entry*)malloc(sizeof(sc_config_pattern_entry));

    int done = 0;
    // int error = 0;
    yaml_event_t event, event_value;

    memset(ret, 0, sizeof(sc_config_pattern_entry));

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            break;
        }

        switch (event.type) {
        case YAML_SCALAR_EVENT:
            if (!yaml_parser_parse(parser, &event_value)) {
                return ret;
            }
            if (strcmp((const char*)event.data.scalar.value, "path") == 0) {
                ret->path = strdup((const char*)event_value.data.scalar.value);
            } else if (strcmp((const char*)event.data.scalar.value, "displayName") == 0) {
                ret->displayName = strdup((const char*)event_value.data.scalar.value);
            } else if (strcmp((const char*)event.data.scalar.value, "rotate") == 0) {
                ret->rotate = strcasecmp((const char*)event_value.data.scalar.value, "true") == 0 ? 1 : 0;
            } else if (strcmp((const char*)event.data.scalar.value, "timestamp") == 0) {
                ret->append_timestamp = strcasecmp((const char*)event_value.data.scalar.value, "true") == 0 ? 1 : 0;
            }
            yaml_event_delete(&event_value);
            break;

        case YAML_MAPPING_END_EVENT:
            yaml_event_delete(&event);
            return ret;

        default:
            az_log(LOG_DEBUG, "event.type = %d", event.type);
            break;
        }

        yaml_event_delete(&event);
    }

    return ret;
}

az_list*
_pick_patterns(yaml_parser_t* parser)
{
    az_list* li = NULL;
    int done = 0;
    int error = 0;

    yaml_event_t event;
    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            error = 1;
            break;
        }

        switch (event.type) {
        case YAML_SEQUENCE_END_EVENT:
            yaml_event_delete(&event);
            return li;

        case YAML_MAPPING_START_EVENT:
            {
                sc_config_pattern_entry *entry = _pick_pattern_entry(parser);
                if (entry) {
                    li = az_list_add(li, entry);
                }
            }
            break;
            /*
        case YAML_MAPPING_END_EVENT:
            yaml_event_delete(&event);
            return 0;
            */
        default:
            az_log(LOG_DEBUG, "%s: event.type = %d", __FUNCTION__, event.type);
            break;
        }

        yaml_event_delete(&event);
    }

    return li;
}

int
_pick_global(yaml_parser_t* parser)
{
    int done = 0;
    int error = 0;
    // int st = 0;

    yaml_event_t event, evvalue;
    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            error = 1;
            break;
        }

        if (event.type != YAML_SCALAR_EVENT) {
            error = 1;
            break;
        }

        if (strcmp((const char*)event.data.scalar.value, "server") == 0) {
            if (!yaml_parser_parse(parser, &evvalue)) {
                error = 1;
                break;
            }

            g_config_server_address = strdup((const char*)evvalue.data.scalar.value);
            yaml_event_delete(&evvalue);
        } else if (strcmp((const char*)event.data.scalar.value, "port") == 0) {
            if (!yaml_parser_parse(parser, &evvalue)) {
                error = 1;
                break;
            }

            g_config_server_port = strtoul((const char*)evvalue.data.scalar.value, NULL, 10);
            yaml_event_delete(&evvalue);
        } else if (strcmp((const char*)event.data.scalar.value, "controlPath") == 0) {
            if (!yaml_parser_parse(parser, &evvalue)) {
                error = 1;
                break;
            }

            g_config_control_path = strdup((const char*)evvalue.data.scalar.value);
            yaml_event_delete(&evvalue);
        } else if (strcmp((const char*)event.data.scalar.value, "waitingSeconds") == 0 ||
                   strcmp((const char*)event.data.scalar.value, "waiting") == 0) {
            if (!yaml_parser_parse(parser, &evvalue)) {
                error = 1;
                break;
            }

            g_config_waiting_seconds = strtoul((const char*)evvalue.data.scalar.value, NULL, 10);
            yaml_event_delete(&evvalue);
        } else if (strcmp((const char*)event.data.scalar.value, "patterns") == 0) {
            g_config_patterns = _pick_patterns(parser);
        }

        switch (evvalue.type) {
            /*
        case YAML_MAPPING_START_EVENT:
            az_log(LOG_DEBUG, "%d) event.data.mapping_start.anchor = [%s] .tag = [%s]",
            lv, event.data.mapping_start.anchor, event.data.mapping_start.tag);
            _pick_mapping(parser, lv + 1);
            break;
        case YAML_MAPPING_END_EVENT:
            yaml_event_delete(&event);
            return 0;
        */

        default:
            az_log(LOG_DEBUG, "event.type = %d", event.type);
            break;
        }

        yaml_event_delete(&event);
    }

    return -1;
}

int
load_config_file(const char* fname)
{
    yaml_parser_t parser;
    yaml_event_t event;
    int done = 0;
    int error = 0;

    FILE *file;

    file = fopen(fname, "rb");
    if (!file) {
        return -1;
    }

    yaml_parser_initialize(&parser);

    yaml_parser_set_input_file(&parser, file);

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            error = 1;
            break;
        }

        switch (event.type) {
        case YAML_SCALAR_EVENT:
            az_log(LOG_DEBUG, "event.data.scalar.value = [%s]",
            event.data.scalar.value);
            break;
        case YAML_SEQUENCE_START_EVENT:
            az_log(LOG_DEBUG, "event.data.sequence_start.anchor = [%s] .tag = [%s]",
            event.data.sequence_start.anchor, event.data.sequence_start.tag);
            break;
        case YAML_MAPPING_START_EVENT:
            az_log(LOG_DEBUG, "event.data.mapping_start.anchor = [%s] .tag = [%s]",
            event.data.mapping_start.anchor, event.data.mapping_start.tag);
            _pick_global(&parser);
            break;
        default:
            az_log(LOG_DEBUG, "event.type = %d", event.type);
            break;
        }
        done = (event.type == YAML_STREAM_END_EVENT);

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);

    fclose(file);

    az_log(LOG_DEBUG, "%s", (error ? "FAILURE" : "SUCCESS"));

    return 0;
}

void
clean_config()
{
    az_list *li = g_config_patterns;
    sc_config_pattern_entry *e;

    free(g_config_server_address);
    free(g_config_control_path);

    for (li = g_config_patterns; li; li = li->next) {
        e = (sc_config_pattern_entry*)li->object;
        
        free(e->path);
        free(e->displayName);
        free(e);
    }

    g_config_server_address = NULL;
    g_config_patterns = NULL;
}
