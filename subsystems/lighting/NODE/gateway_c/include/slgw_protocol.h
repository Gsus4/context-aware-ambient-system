#ifndef SLGW_PROTOCOL_H
#define SLGW_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

#include "slgw_config.h"

typedef struct {
    char req_id[64];
    char status[16];
    char command[32];
    char args[SLGW_ARGS_MAX][SLGW_ARG_TEXT_SIZE];
    int arg_count;
    char raw[SLGW_LINE_SIZE];
    unsigned int uptime_ms;
} slgw_resp_frame;

typedef struct {
    bool valid;
    char event_type[32];
    char code[32];
    char message[SLGW_ARG_TEXT_SIZE];
    unsigned int uptime_ms;
    char raw[SLGW_LINE_SIZE];
} slgw_event_frame;

typedef struct {
    bool valid;
    char protocol_version[16];
    unsigned int uptime_ms;
    char active_mode[32];
    char active_scene[32];
    bool scene_modified;
    int brightness_pct;
    int tone_bias;
    int color_temp_k;
    bool flow_enabled;
    char flow_preset[32];
    char flow_speed[16];
    int flow_brightness;
    bool flow_soft_mode;
    bool breathing_enabled;
    char breathing_speed[16];
    char breathing_strength[16];
    bool manual_override;
    double current_lux;
    int target_lux;
    int tolerance_lux;
    int target_lux_min;
    int target_lux_max;
    int led_output_percent;
    char control_state[32];
    bool sensor_ok;
    char custom_control_type[32];
} slgw_status_frame;

bool slgw_protocol_parse_resp(const char *line, slgw_resp_frame *out);
bool slgw_protocol_parse_event(const char *line, slgw_event_frame *out);
bool slgw_protocol_parse_status(const char *line, slgw_status_frame *out);
int slgw_protocol_build_cmd_line(char *out, size_t out_size, const char *req_id, const char *command, const char *arg_text);
int slgw_protocol_clamp_u8(int value);

#endif
