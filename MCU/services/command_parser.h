#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "services/flow_service.h"
#include "services/light_sensor_service.h"
#include "services/mode_service.h"

#define COMMAND_BUFFER_SIZE 512
#define COMMAND_RESPONSE_SIZE 512

typedef struct
{
    char req_id[64];
    bool success;
    bool should_emit_status;
    bool mode_changed;
    bool scene_changed;
    bool flow_preset_changed;
    bool flow_speed_changed;
    bool flow_brightness_changed;
    bool breathing_changed;
    bool power_changed;
    char command[32];
} command_parser_result_t;

bool command_parser_process_line(char *line,
                                 char *response,
                                 uint16_t response_size,
                                 mode_service_t *mode_service,
                                 light_sensor_service_t *light_service,
                                 bool *report_enabled,
                                 command_parser_result_t *result);

#endif
