#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/time.h"

#include "command_parser.h"
#include "config.h"
#include "utils/status_report.h"
#include "utils/color_utils.h"

static char *trim_whitespace(char *str)
{
    char *end;
    if (str == NULL) return NULL;
    while (*str != '\0' && isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) { *end = '\0'; end--; }
    return str;
}

static void trim_line_end(char *line)
{
    size_t len = strlen(line);
    while (len > 0u && (line[len - 1u] == '\r' || line[len - 1u] == '\n')) { line[--len] = '\0'; }
}

static void str_to_upper(char *str)
{
    while (str != NULL && *str != '\0') { *str = (char)toupper((unsigned char)*str); str++; }
}

static bool parse_int_range(char *text, int minv, int maxv, int *out)
{
    char *endptr = NULL;
    long value;
    text = trim_whitespace(text);
    if (text == NULL || *text == '\0') return false;
    value = strtol(text, &endptr, 10);
    if (endptr == NULL || *endptr != '\0' || value < minv || value > maxv) return false;
    *out = (int)value;
    return true;
}

static bool parse_bool_value(char *text, bool *out)
{
    text = trim_whitespace(text);
    if (text == NULL || out == NULL) return false;
    str_to_upper(text);
    if (strcmp(text, "1") == 0 || strcmp(text, "ON") == 0 || strcmp(text, "TRUE") == 0 || strcmp(text, "ENABLE") == 0 || strcmp(text, "ENABLED") == 0) { *out = true; return true; }
    if (strcmp(text, "0") == 0 || strcmp(text, "OFF") == 0 || strcmp(text, "FALSE") == 0 || strcmp(text, "DISABLE") == 0 || strcmp(text, "DISABLED") == 0) { *out = false; return true; }
    return false;
}

static bool parse_scene(char *text, light_scene_t *scene)
{
    text = trim_whitespace(text); str_to_upper(text);
    if (strcmp(text, "VACANT") == 0) { *scene = LIGHT_SCENE_VACANT; return true; }
    if (strcmp(text, "SLEEP") == 0) { *scene = LIGHT_SCENE_SLEEP; return true; }
    if (strcmp(text, "RELAX") == 0) { *scene = LIGHT_SCENE_RELAX; return true; }
    if (strcmp(text, "WORK") == 0 || strcmp(text, "READING") == 0) { *scene = LIGHT_SCENE_WORK; return true; }
    if (strcmp(text, "EXERCISE") == 0) { *scene = LIGHT_SCENE_EXERCISE; return true; }
    if (strcmp(text, "NIGHT") == 0) { *scene = LIGHT_SCENE_SLEEP; return true; }
    if (strcmp(text, "PARTY") == 0) { *scene = LIGHT_SCENE_EXERCISE; return true; }
    return false;
}

static bool parse_flow_preset(char *text, flow_preset_t *preset)
{
    text = trim_whitespace(text); str_to_upper(text);
    if (strcmp(text, "WARM_AMBIENT") == 0) { *preset = FLOW_PRESET_WARM_AMBIENT; return true; }
    if (strcmp(text, "SUNSET") == 0) { *preset = FLOW_PRESET_SUNSET; return true; }
    if (strcmp(text, "OCEAN") == 0) { *preset = FLOW_PRESET_OCEAN; return true; }
    if (strcmp(text, "AURORA") == 0) { *preset = FLOW_PRESET_AURORA; return true; }
    if (strcmp(text, "LAVENDER") == 0) { *preset = FLOW_PRESET_LAVENDER; return true; }
    if (strcmp(text, "SOFT_RAINBOW") == 0) { *preset = FLOW_PRESET_SOFT_RAINBOW; return true; }
    return false;
}

static bool parse_flow_speed_value(char *text, uint8_t *out_speed)
{
    text = trim_whitespace(text); str_to_upper(text);
    if (strcmp(text, "SLOW") == 0) { *out_speed = 40u; return true; }
    if (strcmp(text, "MEDIUM") == 0) { *out_speed = 80u; return true; }
    if (strcmp(text, "FAST") == 0) { *out_speed = 128u; return true; }
    return false;
}

static bool parse_breathing_speed(char *text, breathing_speed_t *speed)
{
    text = trim_whitespace(text); str_to_upper(text);
    if (strcmp(text, "SLOW") == 0) { *speed = BREATHING_SPEED_SLOW; return true; }
    if (strcmp(text, "MEDIUM") == 0) { *speed = BREATHING_SPEED_MEDIUM; return true; }
    if (strcmp(text, "FAST") == 0) { *speed = BREATHING_SPEED_FAST; return true; }
    return false;
}

static bool parse_breathing_strength(char *text, breathing_strength_t *strength)
{
    text = trim_whitespace(text); str_to_upper(text);
    if (strcmp(text, "LOW") == 0) { *strength = BREATHING_STRENGTH_LOW; return true; }
    if (strcmp(text, "MEDIUM") == 0) { *strength = BREATHING_STRENGTH_MEDIUM; return true; }
    if (strcmp(text, "HIGH") == 0) { *strength = BREATHING_STRENGTH_HIGH; return true; }
    return false;
}

static void init_result(command_parser_result_t *result)
{
    if (result != NULL) memset(result, 0, sizeof(*result));
}

static void set_resp_ok(char *response, uint16_t response_size, const char *req_id, const char *command)
{
    snprintf(response, response_size, "RESP,%s,OK,%s,%lu", req_id, command, (unsigned long)to_ms_since_boot(get_absolute_time()));
}

static void set_resp_err(char *response, uint16_t response_size, const char *req_id, const char *command, const char *code, const char *reason)
{
    snprintf(response, response_size, "RESP,%s,ERR,%s,%s,%s,%lu", req_id, command, code, reason, (unsigned long)to_ms_since_boot(get_absolute_time()));
}

static void copy_req_id(command_parser_result_t *result, const char *req_id, const char *command)
{
    if (result == NULL) return;
    snprintf(result->req_id, sizeof(result->req_id), "%s", req_id);
    snprintf(result->command, sizeof(result->command), "%s", command);
}

bool command_parser_process_line(char *line,
                                 char *response,
                                 uint16_t response_size,
                                 mode_service_t *mode_service,
                                 light_sensor_service_t *light_service,
                                 bool *report_enabled,
                                 command_parser_result_t *result)
{
    char *saveptr = NULL;
    char *frame_type;
    char *req_id;
    char *command;
    char *args;
    (void)light_service;
    (void)report_enabled;

    if (line == NULL || response == NULL || response_size == 0u || mode_service == NULL) return false;
    init_result(result);
    trim_line_end(line);
    frame_type = strtok_r(line, ",", &saveptr);
    req_id = strtok_r(NULL, ",", &saveptr);
    command = strtok_r(NULL, ",", &saveptr);
    args = saveptr;
    if (frame_type == NULL || strcmp(trim_whitespace(frame_type), "CMD") != 0 || req_id == NULL || command == NULL)
    {
        set_resp_err(response, response_size, "unknown", "UNKNOWN", "INVALID_CMD", "invalid_frame");
        return false;
    }
    command = trim_whitespace(command);
    str_to_upper(command);
    copy_req_id(result, req_id, command);

    if (strcmp(command, "GET_STATUS") == 0)
    {
        set_resp_ok(response, response_size, req_id, command);
        if (result) { result->success = true; result->should_emit_status = true; }
        return true;
    }
    if (strcmp(command, "RESTORE_SCENE") == 0)
    {
        if (!mode_service_restore_scene(mode_service))
        {
            set_resp_err(response, response_size, req_id, command, "MODE_CONFLICT", "restore_scene_without_active_scene");
            return false;
        }
        set_resp_ok(response, response_size, req_id, command);
        if (result) { result->success = true; result->scene_changed = true; result->should_emit_status = true; }
        return true;
    }
    if (strcmp(command, "POWER_OFF") == 0)
    {
        if (!mode_service_power_off(mode_service))
        {
            set_resp_err(response, response_size, req_id, command, "EXEC_ERROR", "power_off_failed");
            return false;
        }
        set_resp_ok(response, response_size, req_id, command);
        if (result) { result->success = true; result->power_changed = true; result->should_emit_status = true; }
        return true;
    }

    if (strcmp(command, "SET_SCENE") == 0)
    {
        char args_copy[128];
        char *sp = NULL;
        char *token;
        light_scene_t scene;
        bool has_brightness_override = false;
        int brightness_pct = 0;
        bool has_tone_bias_override = false;
        int tone_bias = 0;
        bool has_color_temp_override = false;
        int color_temp_k = COLOR_TEMP_K_NONE;

        if (args == NULL) { set_resp_err(response, response_size, req_id, command, "MISSING_PARAM", "scene_required"); return false; }
        snprintf(args_copy, sizeof(args_copy), "%s", args);
        token = strtok_r(args_copy, ",", &sp);
        if (token == NULL || !parse_scene(token, &scene)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "invalid_scene"); return false; }

        token = strtok_r(NULL, ",", &sp);
        if (token != NULL) {
            if (!parse_int_range(token, -1, 100, &brightness_pct)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "brightness_out_of_range"); return false; }
            has_brightness_override = (brightness_pct >= 0);
        }

        token = strtok_r(NULL, ",", &sp);
        if (token != NULL) {
            if (!parse_int_range(token, LIGHT_NODE_TONE_BIAS_MIN, LIGHT_NODE_TONE_BIAS_MAX, &tone_bias)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "tone_bias_out_of_range"); return false; }
            has_tone_bias_override = true;
        }

        token = strtok_r(NULL, ",", &sp);
        if (token != NULL) {
            if (!parse_int_range(token, -1, COLOR_TEMP_K_MAX, &color_temp_k)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "color_temp_k_out_of_range"); return false; }
            has_color_temp_override = (color_temp_k >= 0);
            if (has_color_temp_override && !color_utils_is_valid_kelvin(color_temp_k)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "color_temp_k_out_of_range"); return false; }
        }

        if (has_brightness_override || has_tone_bias_override || has_color_temp_override) {
            if (!mode_service_set_scene_with_adjustments(mode_service, scene, has_brightness_override, brightness_pct, has_tone_bias_override, tone_bias, has_color_temp_override, color_temp_k)) { set_resp_err(response, response_size, req_id, command, "EXEC_ERROR", "scene_apply_failed"); return false; }
        } else {
            if (!mode_service_set_scene(mode_service, scene)) { set_resp_err(response, response_size, req_id, command, "EXEC_ERROR", "scene_apply_failed"); return false; }
        }
        set_resp_ok(response, response_size, req_id, command);
        if (result) { result->success = true; result->scene_changed = true; result->should_emit_status = true; }
        return true;
    }

    if (strcmp(command, "SET_STATIC") == 0)
    {
        char args_copy[128]; char *sp = NULL; char *token; int brightness_pct, r, g, b; int color_temp_k = COLOR_TEMP_K_NONE; bool has_color_temp = false;
        if (args == NULL) { set_resp_err(response, response_size, req_id, command, "MISSING_PARAM", "brightness_required"); return false; }
        snprintf(args_copy, sizeof(args_copy), "%s", args);
        token = strtok_r(args_copy, ",", &sp);
        if (token == NULL || !parse_int_range(token, 0, 100, &brightness_pct)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "brightness_out_of_range"); return false; }
        token = strtok_r(NULL, ",", &sp);
        if (token == NULL || !parse_int_range(token, -1, 255, &r)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "rgb_out_of_range"); return false; }
        token = strtok_r(NULL, ",", &sp);
        if (token == NULL || !parse_int_range(token, -1, 255, &g)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "rgb_out_of_range"); return false; }
        token = strtok_r(NULL, ",", &sp);
        if (token == NULL || !parse_int_range(token, -1, 255, &b)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "rgb_out_of_range"); return false; }
        token = strtok_r(NULL, ",", &sp);
        if (token != NULL) {
            if (!parse_int_range(token, COLOR_TEMP_K_MIN, COLOR_TEMP_K_MAX, &color_temp_k)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "color_temp_k_out_of_range"); return false; }
            has_color_temp = true;
        }
        if (has_color_temp) {
            if (!mode_service_set_manual_color_temp(mode_service, (uint8_t)brightness_pct, color_temp_k)) { set_resp_err(response, response_size, req_id, command, "EXEC_ERROR", "static_apply_failed"); return false; }
        } else {
            if (r < 0 || g < 0 || b < 0) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "rgb_out_of_range"); return false; }
            if (!mode_service_set_manual_color(mode_service, (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)brightness_pct)) { set_resp_err(response, response_size, req_id, command, "EXEC_ERROR", "static_apply_failed"); return false; }
        }
        set_resp_ok(response, response_size, req_id, command);
        if (result) { result->success = true; result->mode_changed = true; result->should_emit_status = true; }
        return true;
    }

    if (strcmp(command, "SET_CUSTOM") == 0)
    {
        char args_copy[192];
        char *sp = NULL;
        char *token;
        char control_type[32];
        if (args == NULL) { set_resp_err(response, response_size, req_id, command, "MISSING_PARAM", "custom_control_type_required"); return false; }
        snprintf(args_copy, sizeof(args_copy), "%s", args);
        token = strtok_r(args_copy, ",", &sp);
        if (token == NULL) { set_resp_err(response, response_size, req_id, command, "MISSING_PARAM", "custom_control_type_required"); return false; }
        snprintf(control_type, sizeof(control_type), "%s", trim_whitespace(token));
        str_to_upper(control_type);

        if (strcmp(control_type, "FIXED_BRIGHTNESS") == 0)
        {
            int brightness_pct;
            int color_temp_k = COLOR_TEMP_K_DEFAULT;
            int r = 255, g = 255, b = 255;
            bool use_kelvin = true;
            token = strtok_r(NULL, ",", &sp);
            if (token == NULL || !parse_int_range(token, 0, 100, &brightness_pct)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "brightness_out_of_range"); return false; }
            token = strtok_r(NULL, ",", &sp);
            if (token != NULL)
            {
                char color_mode[16];
                snprintf(color_mode, sizeof(color_mode), "%s", trim_whitespace(token));
                str_to_upper(color_mode);
                use_kelvin = (strcmp(color_mode, "RGB") != 0);
            }
            if (use_kelvin)
            {
                token = strtok_r(NULL, ",", &sp);
                if (token != NULL && !parse_int_range(token, COLOR_TEMP_K_MIN, COLOR_TEMP_K_MAX, &color_temp_k)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "color_temp_k_out_of_range"); return false; }
            }
            else
            {
                token = strtok_r(NULL, ",", &sp);
                if (token == NULL || !parse_int_range(token, 0, 255, &r)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "rgb_out_of_range"); return false; }
                token = strtok_r(NULL, ",", &sp);
                if (token == NULL || !parse_int_range(token, 0, 255, &g)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "rgb_out_of_range"); return false; }
                token = strtok_r(NULL, ",", &sp);
                if (token == NULL || !parse_int_range(token, 0, 255, &b)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "rgb_out_of_range"); return false; }
            }
            if (!mode_service_set_custom_fixed(mode_service, (uint8_t)brightness_pct, color_temp_k, (uint8_t)r, (uint8_t)g, (uint8_t)b, use_kelvin)) { set_resp_err(response, response_size, req_id, command, "EXEC_ERROR", "custom_apply_failed"); return false; }
            set_resp_ok(response, response_size, req_id, command);
            if (result) { result->success = true; result->mode_changed = true; result->should_emit_status = true; }
            return true;
        }

        if (strcmp(control_type, "TARGET_LUX_RANGE") == 0)
        {
            int target_lux, tolerance_lux, min_output, max_output;
            int color_temp_k = COLOR_TEMP_K_DEFAULT;
            int r = 255, g = 255, b = 255;
            bool use_kelvin = true;
            token = strtok_r(NULL, ",", &sp);
            if (token == NULL || !parse_int_range(token, 0, 100000, &target_lux)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "target_lux_out_of_range"); return false; }
            token = strtok_r(NULL, ",", &sp);
            if (token == NULL || !parse_int_range(token, 0, 100000, &tolerance_lux)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "tolerance_lux_out_of_range"); return false; }
            token = strtok_r(NULL, ",", &sp);
            if (token == NULL || !parse_int_range(token, 0, 100, &min_output)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "min_output_out_of_range"); return false; }
            token = strtok_r(NULL, ",", &sp);
            if (token == NULL || !parse_int_range(token, 0, 100, &max_output)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "max_output_out_of_range"); return false; }
            if (max_output < min_output) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "output_range_invalid"); return false; }
            token = strtok_r(NULL, ",", &sp);
            if (token != NULL)
            {
                char color_mode[16];
                snprintf(color_mode, sizeof(color_mode), "%s", trim_whitespace(token));
                str_to_upper(color_mode);
                use_kelvin = (strcmp(color_mode, "RGB") != 0);
            }
            if (use_kelvin)
            {
                token = strtok_r(NULL, ",", &sp);
                if (token != NULL && !parse_int_range(token, COLOR_TEMP_K_MIN, COLOR_TEMP_K_MAX, &color_temp_k)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "color_temp_k_out_of_range"); return false; }
            }
            else
            {
                token = strtok_r(NULL, ",", &sp);
                if (token == NULL || !parse_int_range(token, 0, 255, &r)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "rgb_out_of_range"); return false; }
                token = strtok_r(NULL, ",", &sp);
                if (token == NULL || !parse_int_range(token, 0, 255, &g)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "rgb_out_of_range"); return false; }
                token = strtok_r(NULL, ",", &sp);
                if (token == NULL || !parse_int_range(token, 0, 255, &b)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "rgb_out_of_range"); return false; }
            }
            if (!mode_service_set_custom_target_range(mode_service, target_lux, tolerance_lux, min_output, max_output, color_temp_k, (uint8_t)r, (uint8_t)g, (uint8_t)b, use_kelvin)) { set_resp_err(response, response_size, req_id, command, "EXEC_ERROR", "custom_apply_failed"); return false; }
            set_resp_ok(response, response_size, req_id, command);
            if (result) { result->success = true; result->mode_changed = true; result->should_emit_status = true; }
            return true;
        }

        set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "invalid_custom_control_type");
        return false;
    }

    if (strcmp(command, "SET_BREATHING") == 0)
    {
        char args_copy[64]; char *sp = NULL; char *token; bool enabled = false; breathing_speed_t speed = BREATHING_SPEED_SLOW; breathing_strength_t strength = BREATHING_STRENGTH_LOW;
        if (args == NULL) { set_resp_err(response, response_size, req_id, command, "MISSING_PARAM", "enabled_required"); return false; }
        snprintf(args_copy, sizeof(args_copy), "%s", args);
        token = strtok_r(args_copy, ",", &sp);
        if (token == NULL || !parse_bool_value(token, &enabled)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "invalid_enabled"); return false; }
        if (enabled) {
            token = strtok_r(NULL, ",", &sp);
            if (token == NULL || !parse_breathing_speed(token, &speed)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "breathing_speed_invalid"); return false; }
            token = strtok_r(NULL, ",", &sp);
            if (token == NULL || !parse_breathing_strength(token, &strength)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "breathing_strength_invalid"); return false; }
        }
        if (!mode_service_set_breathing(mode_service, enabled, speed, strength)) { set_resp_err(response, response_size, req_id, command, "MODE_CONFLICT", "static_breathing_only_allowed_in_static_mode"); return false; }
        set_resp_ok(response, response_size, req_id, command);
        if (result) { result->success = true; result->breathing_changed = true; result->should_emit_status = true; }
        return true;
    }

    if (strcmp(command, "SET_AUTO") == 0)
    {
        bool enabled = false;
        if (args == NULL || !parse_bool_value(args, &enabled)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "invalid_enabled"); return false; }
        if (!mode_service_set_auto_mode(mode_service, enabled)) { set_resp_err(response, response_size, req_id, command, "EXEC_ERROR", "set_auto_failed"); return false; }
        set_resp_ok(response, response_size, req_id, command);
        if (result) { result->success = true; result->mode_changed = true; result->should_emit_status = true; }
        return true;
    }

    if (strcmp(command, "SET_FLOW") == 0)
    {
        char args_copy[128]; char *sp = NULL; char *token; flow_preset_t preset; uint8_t speed; int brightness_pct; bool soft_mode;
        if (args == NULL) { set_resp_err(response, response_size, req_id, command, "MISSING_PARAM", "preset_required"); return false; }
        snprintf(args_copy, sizeof(args_copy), "%s", args);
        token = strtok_r(args_copy, ",", &sp);
        if (token == NULL || !parse_flow_preset(token, &preset)) { set_resp_err(response, response_size, req_id, command, "UNSUPPORTED_PRESET", "unsupported_preset"); return false; }
        token = strtok_r(NULL, ",", &sp);
        if (token == NULL || !parse_flow_speed_value(token, &speed)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "flow_speed_invalid"); return false; }
        token = strtok_r(NULL, ",", &sp);
        if (token == NULL || !parse_int_range(token, 0, 100, &brightness_pct)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "flow_brightness_out_of_range"); return false; }
        token = strtok_r(NULL, ",", &sp);
        if (token == NULL || !parse_bool_value(token, &soft_mode)) { set_resp_err(response, response_size, req_id, command, "INVALID_PARAM", "invalid_soft_mode"); return false; }
        if (soft_mode && preset == FLOW_PRESET_SOFT_RAINBOW) { set_resp_err(response, response_size, req_id, command, "UNSUPPORTED_SOFT_MODE", "soft_rainbow_not_supported"); return false; }
        if (soft_mode && speed > 80u) { set_resp_err(response, response_size, req_id, command, "UNSUPPORTED_SOFT_MODE", "flow_speed_fast_not_allowed_in_soft_mode"); return false; }
        if (soft_mode && brightness_pct > FLOW_SOFT_MODE_BRIGHTNESS_MAX_PCT) { set_resp_err(response, response_size, req_id, command, "UNSUPPORTED_SOFT_MODE", preset == FLOW_PRESET_AURORA ? "aurora_exceeds_soft_mode_limit" : "flow_brightness_exceeds_soft_mode_limit"); return false; }
        if (soft_mode && preset == FLOW_PRESET_AURORA && brightness_pct > 50) { set_resp_err(response, response_size, req_id, command, "UNSUPPORTED_SOFT_MODE", "aurora_exceeds_soft_mode_limit"); return false; }
        if (!mode_service_set_flow_preset(mode_service, preset) || !mode_service_set_flow_speed(mode_service, speed) || !mode_service_set_flow_brightness(mode_service, (uint8_t)brightness_pct) || !mode_service_set_flow_soft_mode(mode_service, soft_mode)) { set_resp_err(response, response_size, req_id, command, "EXEC_ERROR", "flow_apply_failed"); return false; }
        set_resp_ok(response, response_size, req_id, command);
        if (result) { result->success = true; result->flow_preset_changed = true; result->flow_speed_changed = true; result->flow_brightness_changed = true; result->should_emit_status = true; }
        return true;
    }

    set_resp_err(response, response_size, req_id, command, "INVALID_CMD", "unknown_command");
    return false;
}
