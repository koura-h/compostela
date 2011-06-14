/* $Id$ */
#include <yaml.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "appconfig.h"

#include "azlist.h"
#include "azlog.h"


char*    g_config_server_logdir = NULL;
char*    g_config_server_addr = NULL;
int      g_config_server_port = 0;
int      g_config_hostname_lookups = 0;

az_list* g_config_aggregate_context_list = NULL;


static sc_aggregate_context*
_pick_aggregate_context(yaml_parser_t* parser)
{
    sc_aggregate_context* ret =
    (sc_aggregate_context*)malloc(sizeof(sc_aggregate_context));

    int done = 0;
    // int error = 0;
    yaml_event_t event, event_value;

    memset(ret, 0, sizeof(sc_aggregate_context));
    ret->f_merge = 1;
    ret->f_separate = 1;
    ret->f_rotate = 1;

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
                az_log(LOG_DEBUG, "ret->path = %s", ret->path);
            } else if (strcmp((const char*)event.data.scalar.value, "displayName") == 0) {
                ret->displayName = strdup((const char*)event_value.data.scalar.value);
            } else if (strcmp((const char*)event.data.scalar.value, "rotate") == 0) {
                ret->f_rotate = strcasecmp((const char*)event_value.data.scalar.value, "true") == 0 ? 1 : 0;
            } else if (strcmp((const char*)event.data.scalar.value, "timestamp") == 0) {
                ret->f_timestamp = strcasecmp((const char*)event_value.data.scalar.value, "true") == 0 ? 1 : 0;
            } else if (strcmp((const char*)event.data.scalar.value, "mode") == 0) {
                if (strcasecmp((const char*)event_value.data.scalar.value, "both") == 0) {
                    ret->f_separate = 1;
                    ret->f_merge    = 1;
                } else if (strcasecmp((const char*)event_value.data.scalar.value, "separate") == 0) {
                    ret->f_separate = 1;
                    ret->f_merge    = 0;
                } else if (strcasecmp((const char*)event_value.data.scalar.value, "merge") == 0) {
                    ret->f_separate = 0;
                    ret->f_merge    = 1;
                }
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


static az_list*
_pick_patterns(yaml_parser_t* parser)
{
    az_list *li = NULL;
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
                sc_aggregate_context *cxt = _pick_aggregate_context(parser);
                if (cxt) {
                    az_log(LOG_DEBUG, "cxt = %p", cxt);
                    li = az_list_add(li, cxt);
                }
            }
            break;

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

        if (strcmp((const char*)event.data.scalar.value, "logdir") == 0) {
            if (!yaml_parser_parse(parser, &evvalue)) {
                error = 1;
                break;
            }

            g_config_server_logdir = strdup((const char*)evvalue.data.scalar.value);
            yaml_event_delete(&evvalue);
        } else if (strcmp((const char*)event.data.scalar.value, "port") == 0) {
            if (!yaml_parser_parse(parser, &evvalue)) {
                error = 1;
                break;
            }

            g_config_server_port = strtoul((const char*)evvalue.data.scalar.value, NULL, 10);
            yaml_event_delete(&evvalue);
        } else if (strcmp((const char*)event.data.scalar.value, "hostnameLookups") == 0) {
            if (!yaml_parser_parse(parser, &evvalue)) {
                error = 1;
                break;
            }

            g_config_hostname_lookups = (strcmp((const char*)evvalue.data.scalar.value, "true") == 0 ? 1 : 0);
            yaml_event_delete(&evvalue);
        } else if (strcmp((const char*)event.data.scalar.value, "patterns") == 0) {
            assert(g_config_aggregate_context_list == NULL);
            g_config_aggregate_context_list = _pick_patterns(parser);
            g_config_aggregate_context_list = az_list_reverse(g_config_aggregate_context_list);
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
    az_list* li;
    for (li = g_config_aggregate_context_list; li; li = li->next) {
        sc_aggregate_context* cxt = li->object;
        free(cxt->path);
        free(cxt->displayName);
        free(cxt);
    }
    az_list_delete_all(g_config_aggregate_context_list);

    free(g_config_server_logdir);
    g_config_server_logdir = NULL;
    free(g_config_server_addr);
    g_config_server_addr = NULL;
}
