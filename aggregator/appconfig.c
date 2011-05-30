/* $Id$ */
#include <yaml.h>

#include <stdlib.h>
#include <stdio.h>

#include "appconfig.h"

#include "sclog.h"

char* g_config_server_logdir = NULL;
char* g_config_server_addr = NULL;
int g_config_server_port = 0;

int
_pick_global(yaml_parser_t* parser)
{
    int done = 0;
    int error = 0;
    int st = 0;

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

        if (strcmp(event.data.scalar.value, "logdir") == 0) {
            if (!yaml_parser_parse(parser, &evvalue)) {
                error = 1;
                break;
            }

            g_config_server_logdir = strdup(evvalue.data.scalar.value);
            yaml_event_delete(&evvalue);
        } else if (strcmp(event.data.scalar.value, "port") == 0) {
            if (!yaml_parser_parse(parser, &evvalue)) {
                error = 1;
                break;
            }

            g_config_server_port = strtoul(evvalue.data.scalar.value, NULL, 10);
            yaml_event_delete(&evvalue);
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
            sc_log(LOG_DEBUG, "event.data.scalar.value = [%s]",
            event.data.scalar.value);
            break;
        case YAML_SEQUENCE_START_EVENT:
            sc_log(LOG_DEBUG, "event.data.sequence_start.anchor = [%s] .tag = [%s]",
            event.data.sequence_start.anchor, event.data.sequence_start.tag);
            break;
        case YAML_MAPPING_START_EVENT:
            sc_log(LOG_DEBUG, "event.data.mapping_start.anchor = [%s] .tag = [%s]",
            event.data.mapping_start.anchor, event.data.mapping_start.tag);
            _pick_global(&parser);
            break;
        default:
            sc_log(LOG_DEBUG, "event.type = %d", event.type);
            break;
        }
        done = (event.type == YAML_STREAM_END_EVENT);

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);

    fclose(file);

    sc_log(LOG_DEBUG, "%s", (error ? "FAILURE" : "SUCCESS"));

    return 0;
}

void
clean_config()
{
    free(g_config_server_logdir);
    g_config_server_logdir = NULL;
    free(g_config_server_addr);
    g_config_server_addr = NULL;
}
