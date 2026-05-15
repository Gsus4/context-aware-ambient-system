#ifndef SLGW_JSON_H
#define SLGW_JSON_H

#include <stdbool.h>
#include <stddef.h>

#include "slgw_config.h"
#include "slgw_protocol.h"

typedef enum {
    SLGW_CMD_NONE = 0,
    SLGW_CMD_GET_STATUS,
    SLGW_CMD_SET_SCENE,
    SLGW_CMD_RESTORE_SCENE,
    SLGW_CMD_SET_STATIC,
    SLGW_CMD_SET_STATIC_BREATHING,
    SLGW_CMD_SET_CUSTOM,
    SLGW_CMD_SET_AUTO,
    SLGW_CMD_SET_FLOW,
    SLGW_CMD_POWER_OFF
} slgw_command_type;

typedef struct {
    bool from_integration;
    char request_id[64];
    char source[64];
    char source_topic[SLGW_TOPIC_SIZE];
    int timeout_override_ms;
} slgw_command_context;

typedef struct {
    bool valid;
    slgw_command_type type;
    char cmd_name[32];
    char mode[32];
    char scene[32];
    bool scene_brightness_set;
    int scene_brightness_pct;
    bool tone_bias_set;
    int tone_bias;
    bool scene_color_temp_k_set;
    int scene_color_temp_k;
    char preset[32];
    int static_r;
    int static_g;
    int static_b;
    int static_brightness_pct;
    bool static_color_temp_k_set;
    int static_color_temp_k;
    char custom_control_type[32];
    int fixed_brightness_pct;
    int target_lux;
    int tolerance_lux;
    int min_output;
    int max_output;
    char color_mode[16];
    int custom_color_temp_k;
    int custom_r;
    int custom_g;
    int custom_b;
    bool breathing_enabled;
    char breathing_speed[16];
    char breathing_strength[16];
    bool auto_request_enabled;
    char flow_speed[16];
    int flow_brightness;
    bool flow_soft_mode;
    int gradient_c1[3];
    int gradient_c2[3];
    int gradient_c3[3];
    slgw_command_context context;
    char error_message[128];
} slgw_command_request;

int slgw_json_parse_command(const char *topic, const char *payload, const slgw_config *cfg, slgw_command_request *out);
int slgw_json_build_ack(char *out, size_t out_size, const slgw_config *cfg, const slgw_command_request *request, bool ok, const char *message, const slgw_resp_frame *resp, const char *error_type);
int slgw_json_build_status(char *out, size_t out_size, const slgw_config *cfg, const slgw_status_frame *status, const char *req_id);
int slgw_json_build_status_integration(char *out, size_t out_size, const slgw_config *cfg, const slgw_status_frame *status);
int slgw_json_build_event(char *out, size_t out_size, const slgw_config *cfg, const slgw_event_frame *event_frame);
int slgw_json_build_event_integration(char *out, size_t out_size, const slgw_config *cfg, const slgw_event_frame *event_frame);
int slgw_json_build_availability_internal(char *out, size_t out_size, const slgw_config *cfg, bool online, const char *reason);
int slgw_json_build_availability_integration(char *out, size_t out_size, const slgw_config *cfg, bool online, const char *reason);

#endif
