#define _POSIX_C_SOURCE 200809L
#include "slgw_json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "slgw_text.h"
#include "slgw_topics.h"

#define SLGW_COLOR_TEMP_K_MIN 2700
#define SLGW_COLOR_TEMP_K_MAX 6500

static void slgw_now_ts_iso(char *out, size_t out_size) {
    time_t now = time(NULL);
    struct tm tm_now;
    char base[32];
    char zone[8];

    if (out == NULL || out_size == 0) {
        return;
    }

    localtime_r(&now, &tm_now);
    strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tm_now);
    strftime(zone, sizeof(zone), "%z", &tm_now);
    if (strlen(zone) == 5) {
        slgw_format(out, out_size, "%s%c%c%c:%c%c", base, zone[0], zone[1], zone[2], zone[3], zone[4]);
    } else {
        slgw_format(out, out_size, "%s+00:00", base);
    }
}

static const char *skip_ws(const char *s) {
    while (s != NULL && *s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

static bool is_object(const char *text) {
    const char *cursor;
    const char *last_non_ws = NULL;
    int depth_obj = 0;
    int depth_arr = 0;
    bool in_string = false;
    bool escape = false;

    text = skip_ws(text);
    if (text == NULL || *text != '{') {
        return false;
    }

    cursor = text;
    while (*cursor != '\0') {
        char ch = *cursor;
        if (!isspace((unsigned char)ch)) {
            last_non_ws = cursor;
        }
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                in_string = false;
            }
        } else {
            if (ch == '"') {
                in_string = true;
            } else if (ch == '{') {
                depth_obj++;
            } else if (ch == '}') {
                depth_obj--;
                if (depth_obj < 0) {
                    return false;
                }
            } else if (ch == '[') {
                depth_arr++;
            } else if (ch == ']') {
                depth_arr--;
                if (depth_arr < 0) {
                    return false;
                }
            }
        }
        cursor++;
    }

    return !in_string && !escape && depth_obj == 0 && depth_arr == 0 && last_non_ws != NULL && *last_non_ws == '}';
}

static const char *find_key(const char *json, const char *key) {
    char pattern[64];
    if (slgw_format(pattern, sizeof(pattern), "\"%s\"", key) != 0) {
        return NULL;
    }
    return strstr(json, pattern);
}

static int extract_raw_field(const char *json, const char *key, char *out, size_t out_size) {
    const char *pos = find_key(json, key);
    const char *start;
    const char *cursor;
    int depth_obj = 0;
    int depth_arr = 0;
    bool in_string = false;
    bool escape = false;
    size_t len;

    if (pos == NULL) {
        return -1;
    }
    pos = strchr(pos, ':');
    if (pos == NULL) {
        return -1;
    }
    start = skip_ws(pos + 1);
    if (start == NULL || *start == '\0') {
        return -1;
    }

    cursor = start;
    while (*cursor != '\0') {
        char ch = *cursor;
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                in_string = false;
            }
        } else {
            if (ch == '"') {
                in_string = true;
            } else if (ch == '{') {
                depth_obj++;
            } else if (ch == '}') {
                if (depth_obj == 0 && depth_arr == 0) {
                    break;
                }
                depth_obj--;
            } else if (ch == '[') {
                depth_arr++;
            } else if (ch == ']') {
                depth_arr--;
            } else if (ch == ',' && depth_obj == 0 && depth_arr == 0) {
                break;
            }
        }
        cursor++;
    }

    len = (size_t)(cursor - start);
    while (len > 0 && isspace((unsigned char)start[len - 1])) {
        len--;
    }
    if (len + 1 > out_size) {
        return -1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

static int extract_string_field(const char *json, const char *key, char *out, size_t out_size) {
    char raw[SLGW_JSON_SIZE];
    size_t len;

    if (extract_raw_field(json, key, raw, sizeof(raw)) != 0) {
        return -1;
    }
    if (raw[0] != '"') {
        return -1;
    }
    len = strlen(raw);
    if (len < 2 || raw[len - 1] != '"') {
        return -1;
    }
    raw[len - 1] = '\0';
    return slgw_copy_text(out, out_size, raw + 1);
}

static int extract_bool_field(const char *json, const char *key, bool *out) {
    char raw[32];

    if (extract_raw_field(json, key, raw, sizeof(raw)) != 0) {
        return -1;
    }
    if (strcasecmp(raw, "true") == 0 || strcmp(raw, "1") == 0) {
        *out = true;
        return 0;
    }
    if (strcasecmp(raw, "false") == 0 || strcmp(raw, "0") == 0) {
        *out = false;
        return 0;
    }
    return -1;
}

static int extract_int_field(const char *json, const char *key, int *out) {
    char raw[32];

    if (extract_raw_field(json, key, raw, sizeof(raw)) != 0) {
        return -1;
    }
    return slgw_parse_int(raw, out);
}

static int extract_object_field(const char *json, const char *key, char *out, size_t out_size) {
    if (extract_raw_field(json, key, out, out_size) != 0) {
        return -1;
    }
    return is_object(out) ? 0 : -1;
}

static int extract_string_field_any(const char *json,
                                    const char *key1,
                                    const char *key2,
                                    char *out,
                                    size_t out_size) {
    if (key1 != NULL && extract_string_field(json, key1, out, out_size) == 0) {
        return 0;
    }
    if (key2 != NULL && extract_string_field(json, key2, out, out_size) == 0) {
        return 0;
    }
    return -1;
}

static int extract_int_field_any(const char *json,
                                 const char *key1,
                                 const char *key2,
                                 const char *key3,
                                 int *out) {
    if (key1 != NULL && extract_int_field(json, key1, out) == 0) {
        return 0;
    }
    if (key2 != NULL && extract_int_field(json, key2, out) == 0) {
        return 0;
    }
    if (key3 != NULL && extract_int_field(json, key3, out) == 0) {
        return 0;
    }
    return -1;
}

static int fill_context(slgw_command_context *context, const char *topic, const char *payload) {
    memset(context, 0, sizeof(*context));
    if (slgw_copy_text(context->source_topic, sizeof(context->source_topic), topic == NULL ? "" : topic) != 0) {
        return -1;
    }
    (void)extract_string_field(payload, "req_id", context->request_id, sizeof(context->request_id));
    return 0;
}

static int bool_to_text(bool value, char *out, size_t out_size) {
    return slgw_copy_text(out, out_size, value ? "true" : "false");
}


static const char *slgw_external_active_mode(const slgw_status_frame *status) {
    if (status == NULL) {
        return "unknown";
    }
    if (strcmp(status->active_mode, "scene") == 0) {
        return "scene";
    }
    if (strcmp(status->active_mode, "auto") == 0) {
        return "scene";
    }
    return "custom";
}

static const char *slgw_custom_submode(const slgw_status_frame *status) {
    if (status == NULL) {
        return NULL;
    }
    if (strcmp(status->active_mode, "scene") == 0 || strcmp(status->active_mode, "auto") == 0) {
        return NULL;
    }
    if (status->flow_enabled) {
        return status->flow_soft_mode ? "combo" : "flow";
    }
    if (status->breathing_enabled) {
        return "breathing";
    }
    if (status->custom_control_type[0] != '\0') {
        return status->custom_control_type;
    }
    return "static";
}

static int slgw_json_string_or_null(char *out, size_t out_size, const char *value) {
    if (value == NULL || value[0] == '\0') {
        return slgw_copy_text(out, out_size, "null");
    }
    return slgw_format(out, out_size, "\"%s\"", value);
}

static int slgw_json_req_id_value(char *out, size_t out_size, const char *req_id) {
    if (req_id == NULL || req_id[0] == '\0') {
        return slgw_copy_text(out, out_size, "null");
    }
    return slgw_format(out, out_size, "\"%s\"", req_id);
}

int slgw_json_parse_command(const char *topic, const char *payload, const slgw_config *cfg, slgw_command_request *out) {
    char command_topic[SLGW_TOPIC_SIZE];
    char params[SLGW_JSON_SIZE];

    memset(out, 0, sizeof(*out));
    if (payload == NULL || !is_object(payload)) {
        slgw_copy_text(out->error_message, sizeof(out->error_message), "invalid_json_object");
        return -1;
    }
    if (slgw_topic_server_set(command_topic, sizeof(command_topic), cfg) != 0) {
        return -1;
    }
    if (topic != NULL && strcmp(topic, command_topic) != 0) {
        slgw_copy_text(out->error_message, sizeof(out->error_message), "unsupported_topic");
        return -1;
    }
    if (fill_context(&out->context, topic, payload) != 0) {
        return -1;
    }
    if (extract_string_field(payload, "cmd", out->cmd_name, sizeof(out->cmd_name)) != 0) {
        slgw_copy_text(out->error_message, sizeof(out->error_message), "missing_command");
        return -1;
    }
    slgw_copy_lower_text(out->cmd_name, sizeof(out->cmd_name), out->cmd_name);
    if (extract_object_field(payload, "params", params, sizeof(params)) != 0) {
        slgw_copy_text(params, sizeof(params), "{}");
    }

    if (strcmp(out->cmd_name, "get_status") == 0) {
        out->type = SLGW_CMD_GET_STATUS;
    } else if (strcmp(out->cmd_name, "set_scene") == 0) {
        if (extract_string_field(params, "scene", out->scene, sizeof(out->scene)) != 0) {
            slgw_copy_text(out->error_message, sizeof(out->error_message), "missing_scene");
            return -1;
        }
        slgw_copy_upper_text(out->scene, sizeof(out->scene), out->scene);
        if (extract_int_field(params, "brightness_pct", &out->scene_brightness_pct) == 0) {
            out->scene_brightness_set = true;
        }
        if (extract_int_field(params, "tone_bias", &out->tone_bias) == 0) {
            if (out->tone_bias < -2 || out->tone_bias > 2) {
                slgw_copy_text(out->error_message, sizeof(out->error_message), "tone_bias_out_of_range");
                return -1;
            }
            out->tone_bias_set = true;
        }
        if (extract_int_field(params, "color_temp_k", &out->scene_color_temp_k) == 0) {
            if (out->scene_color_temp_k < SLGW_COLOR_TEMP_K_MIN || out->scene_color_temp_k > SLGW_COLOR_TEMP_K_MAX) {
                slgw_copy_text(out->error_message, sizeof(out->error_message), "color_temp_k_out_of_range");
                return -1;
            }
            out->scene_color_temp_k_set = true;
        }
        out->type = SLGW_CMD_SET_SCENE;
    } else if (strcmp(out->cmd_name, "restore_scene") == 0) {
        out->type = SLGW_CMD_RESTORE_SCENE;
    } else if (strcmp(out->cmd_name, "set_static") == 0) {
        if (extract_int_field(params, "brightness_pct", &out->static_brightness_pct) != 0) {
            slgw_copy_text(out->error_message, sizeof(out->error_message), "invalid_static_params");
            return -1;
        }
        if (out->static_brightness_pct < 0 || out->static_brightness_pct > 100) {
            slgw_copy_text(out->error_message, sizeof(out->error_message), "brightness_out_of_range");
            return -1;
        }
        if (extract_int_field(params, "color_temp_k", &out->static_color_temp_k) == 0) {
            if (out->static_color_temp_k < SLGW_COLOR_TEMP_K_MIN || out->static_color_temp_k > SLGW_COLOR_TEMP_K_MAX) {
                slgw_copy_text(out->error_message, sizeof(out->error_message), "color_temp_k_out_of_range");
                return -1;
            }
            out->static_color_temp_k_set = true;
        } else if (extract_int_field(params, "r", &out->static_r) != 0 ||
                   extract_int_field(params, "g", &out->static_g) != 0 ||
                   extract_int_field(params, "b", &out->static_b) != 0) {
            slgw_copy_text(out->error_message, sizeof(out->error_message), "invalid_static_params");
            return -1;
        }
        out->static_r = slgw_protocol_clamp_u8(out->static_r);
        out->static_g = slgw_protocol_clamp_u8(out->static_g);
        out->static_b = slgw_protocol_clamp_u8(out->static_b);
        out->type = SLGW_CMD_SET_STATIC;
    } else if (strcmp(out->cmd_name, "set_custom") == 0) {
        if (extract_string_field(params, "custom_control_type", out->custom_control_type, sizeof(out->custom_control_type)) != 0) {
            slgw_copy_text(out->error_message, sizeof(out->error_message), "missing_custom_control_type");
            return -1;
        }
        slgw_copy_lower_text(out->custom_control_type, sizeof(out->custom_control_type), out->custom_control_type);
        slgw_copy_text(out->color_mode, sizeof(out->color_mode), "cct");
        (void)extract_string_field(params, "color_mode", out->color_mode, sizeof(out->color_mode));
        slgw_copy_lower_text(out->color_mode, sizeof(out->color_mode), out->color_mode);
        out->custom_color_temp_k = 4000;
        out->custom_r = 255;
        out->custom_g = 255;
        out->custom_b = 255;
        if (strcmp(out->color_mode, "rgb") == 0) {
            char rgb_obj[SLGW_JSON_SIZE];
            if (extract_object_field(params, "rgb", rgb_obj, sizeof(rgb_obj)) != 0 ||
                extract_int_field(rgb_obj, "r", &out->custom_r) != 0 ||
                extract_int_field(rgb_obj, "g", &out->custom_g) != 0 ||
                extract_int_field(rgb_obj, "b", &out->custom_b) != 0) {
                slgw_copy_text(out->error_message, sizeof(out->error_message), "invalid_rgb");
                return -1;
            }
            out->custom_r = slgw_protocol_clamp_u8(out->custom_r);
            out->custom_g = slgw_protocol_clamp_u8(out->custom_g);
            out->custom_b = slgw_protocol_clamp_u8(out->custom_b);
        } else if (extract_int_field(params, "color_temp_k", &out->custom_color_temp_k) == 0) {
            if (out->custom_color_temp_k < SLGW_COLOR_TEMP_K_MIN || out->custom_color_temp_k > SLGW_COLOR_TEMP_K_MAX) {
                slgw_copy_text(out->error_message, sizeof(out->error_message), "color_temp_k_out_of_range");
                return -1;
            }
        }
        if (strcmp(out->custom_control_type, "fixed_brightness") == 0) {
            if (extract_int_field(params, "fixed_brightness_pct", &out->fixed_brightness_pct) != 0 ||
                out->fixed_brightness_pct < 0 || out->fixed_brightness_pct > 100) {
                slgw_copy_text(out->error_message, sizeof(out->error_message), "brightness_out_of_range");
                return -1;
            }
            out->type = SLGW_CMD_SET_CUSTOM;
        } else if (strcmp(out->custom_control_type, "target_lux_range") == 0) {
            if (extract_int_field(params, "target_lux", &out->target_lux) != 0 ||
                extract_int_field(params, "tolerance_lux", &out->tolerance_lux) != 0 ||
                extract_int_field(params, "min_output", &out->min_output) != 0 ||
                extract_int_field(params, "max_output", &out->max_output) != 0 ||
                out->target_lux < 0 || out->tolerance_lux < 0 ||
                out->min_output < 0 || out->min_output > 100 ||
                out->max_output < 0 || out->max_output > 100 ||
                out->max_output < out->min_output) {
                slgw_copy_text(out->error_message, sizeof(out->error_message), "invalid_target_lux_range");
                return -1;
            }
            out->type = SLGW_CMD_SET_CUSTOM;
        } else {
            slgw_copy_text(out->error_message, sizeof(out->error_message), "invalid_custom_control_type");
            return -1;
        }
    } else if (strcmp(out->cmd_name, "set_static_breathing") == 0) {
        if (extract_bool_field(params, "enabled", &out->breathing_enabled) != 0) {
            slgw_copy_text(out->error_message, sizeof(out->error_message), "missing_breathing_enabled");
            return -1;
        }
        if (out->breathing_enabled) {
            if (extract_string_field(params, "speed", out->breathing_speed, sizeof(out->breathing_speed)) != 0 ||
                extract_string_field(params, "strength", out->breathing_strength, sizeof(out->breathing_strength)) != 0) {
                slgw_copy_text(out->error_message, sizeof(out->error_message), "invalid_breathing_params");
                return -1;
            }
            slgw_copy_upper_text(out->breathing_speed, sizeof(out->breathing_speed), out->breathing_speed);
            slgw_copy_upper_text(out->breathing_strength, sizeof(out->breathing_strength), out->breathing_strength);
        } else {
            slgw_copy_text(out->breathing_speed, sizeof(out->breathing_speed), "SLOW");
            slgw_copy_text(out->breathing_strength, sizeof(out->breathing_strength), "LOW");
        }
        out->type = SLGW_CMD_SET_STATIC_BREATHING;
    } else if (strcmp(out->cmd_name, "set_auto") == 0) {
        if (extract_bool_field(params, "enabled", &out->auto_request_enabled) != 0) {
            slgw_copy_text(out->error_message, sizeof(out->error_message), "missing_enabled");
            return -1;
        }
        out->type = SLGW_CMD_SET_AUTO;
    } else if (strcmp(out->cmd_name, "set_flow") == 0) {
        if (extract_string_field_any(params, "flow_preset", "preset", out->preset, sizeof(out->preset)) != 0 ||
            extract_string_field_any(params, "flow_speed", "speed", out->flow_speed, sizeof(out->flow_speed)) != 0 ||
            extract_int_field_any(params, "flow_brightness", "brightness_pct", "flow_brightness_pct", &out->flow_brightness) != 0 ||
            extract_bool_field(params, "soft_mode", &out->flow_soft_mode) != 0) {
            slgw_copy_text(out->error_message, sizeof(out->error_message), "invalid_flow_params");
            return -1;
        }
        slgw_copy_upper_text(out->preset, sizeof(out->preset), out->preset);
        slgw_copy_upper_text(out->flow_speed, sizeof(out->flow_speed), out->flow_speed);
        out->type = SLGW_CMD_SET_FLOW;
    } else if (strcmp(out->cmd_name, "power_off") == 0) {
        out->type = SLGW_CMD_POWER_OFF;
    } else {
        slgw_copy_text(out->error_message, sizeof(out->error_message), "unsupported_command");
        return -1;
    }

    out->valid = true;
    return 0;
}

int slgw_json_build_ack(char *out,
                        size_t out_size,
                        const slgw_config *cfg,
                        const slgw_command_request *request,
                        bool ok,
                        const char *message,
                        const slgw_resp_frame *resp,
                        const char *error_type) {
    char ts[40];
    char ok_text[8];
    char req_id_json[80];
    const char *code = "OK";
    const char *reason = "";
    unsigned int uptime_ms = 0;
    bool success;

    if (out == NULL || out_size == 0 || cfg == NULL || request == NULL) {
        return -1;
    }

    slgw_now_ts_iso(ts, sizeof(ts));
    success = ok && (resp == NULL || strcmp(resp->status, "OK") == 0);
    if (resp != NULL) {
        uptime_ms = resp->uptime_ms;
    }

    if (!success) {
        if (resp != NULL && resp->arg_count >= 2) {
            code = resp->args[0];
            reason = resp->args[1];
        } else {
            code = (error_type != NULL && error_type[0] != '\0') ? error_type : "EXEC_ERROR";
            reason = (message != NULL && message[0] != '\0') ? message : "exec_error";
        }
    }

    if (slgw_json_req_id_value(req_id_json, sizeof(req_id_json), request->context.request_id) != 0) {
        return -1;
    }
    if (bool_to_text(success, ok_text, sizeof(ok_text)) != 0) {
        return -1;
    }

    if (success && strcmp(request->cmd_name, "set_auto") == 0) {
        return slgw_format(
            out,
            out_size,
            "{\"req_id\":%s,\"ts\":\"%s\",\"type\":\"ack\",\"node_id\":\"%s\",\"command\":\"%s\",\"ok\":%s,\"code\":\"DEPRECATED_AUTO_MAPPED_TO_SCENE\",\"reason\":\"mapped_to_scene\",\"uptime_ms\":%u,\"deprecated\":true,\"mapped_to\":{\"mode\":\"scene\",\"scene\":\"work\"}}",
            req_id_json,
            ts,
            cfg->node_id,
            request->cmd_name,
            ok_text,
            uptime_ms);
    }

    return slgw_format(
        out,
        out_size,
        "{\"req_id\":%s,\"ts\":\"%s\",\"type\":\"ack\",\"node_id\":\"%s\",\"command\":\"%s\",\"ok\":%s,\"code\":\"%s\",\"reason\":\"%s\",\"uptime_ms\":%u}",
        req_id_json,
        ts,
        cfg->node_id,
        request->cmd_name,
        ok_text,
        code,
        reason,
        uptime_ms);
}

int slgw_json_build_status(char *out, size_t out_size, const slgw_config *cfg, const slgw_status_frame *status, const char *req_id) {
    char ts[40];
    char req_id_json[80];
    char scene_modified[8];
    char flow_enabled[8];
    char flow_soft_mode[8];
    char breathing_enabled[8];
    char manual_override[8];
    char sensor_ok[8];
    char custom_submode_json[32];
    const char *external_mode;
    const char *custom_submode;
    bool reported_scene_modified;
    int reported_tone_bias;
    int reported_color_temp_k;

    if (out == NULL || out_size == 0 || cfg == NULL || status == NULL) {
        return -1;
    }

    external_mode = slgw_external_active_mode(status);
    custom_submode = slgw_custom_submode(status);
    reported_scene_modified = (strcmp(external_mode, "scene") == 0) ? status->scene_modified : false;
    reported_tone_bias = (strcmp(external_mode, "scene") == 0) ? status->tone_bias : 0;
    reported_color_temp_k = status->color_temp_k;

    slgw_now_ts_iso(ts, sizeof(ts));
    if (slgw_json_req_id_value(req_id_json, sizeof(req_id_json), req_id) != 0) {
        return -1;
    }
    if (bool_to_text(reported_scene_modified, scene_modified, sizeof(scene_modified)) != 0 ||
        bool_to_text(status->flow_enabled, flow_enabled, sizeof(flow_enabled)) != 0 ||
        bool_to_text(status->flow_soft_mode, flow_soft_mode, sizeof(flow_soft_mode)) != 0 ||
        bool_to_text(status->breathing_enabled, breathing_enabled, sizeof(breathing_enabled)) != 0 ||
        bool_to_text(status->manual_override, manual_override, sizeof(manual_override)) != 0 ||
        bool_to_text(status->sensor_ok, sensor_ok, sizeof(sensor_ok)) != 0 ||
        slgw_json_string_or_null(custom_submode_json, sizeof(custom_submode_json), custom_submode) != 0) {
        return -1;
    }

    return slgw_format(
        out,
        out_size,
        "{\"req_id\":%s,\"ts\":\"%s\",\"type\":\"status\",\"node_id\":\"%s\",\"uptime_ms\":%u,\"active_mode\":\"%s\",\"active_scene\":\"%s\",\"custom_submode\":%s,\"scene_modified\":%s,\"brightness_pct\":%d,\"tone_bias\":%d,\"color_temp_k\":%d,\"flow_enabled\":%s,\"flow_preset\":\"%s\",\"flow_speed\":\"%s\",\"flow_brightness\":%d,\"flow_soft_mode\":%s,\"breathing_enabled\":%s,\"breathing_speed\":\"%s\",\"breathing_strength\":\"%s\",\"manual_override\":%s,\"current_lux\":%.1f,\"target_lux\":%d,\"tolerance_lux\":%d,\"target_lux_min\":%d,\"target_lux_max\":%d,\"led_output_percent\":%d,\"control_state\":\"%s\",\"sensor_ok\":%s}",
        req_id_json,
        ts,
        cfg->node_id,
        status->uptime_ms,
        external_mode,
        status->active_scene,
        custom_submode_json,
        scene_modified,
        status->brightness_pct,
        reported_tone_bias,
        reported_color_temp_k,
        flow_enabled,
        status->flow_preset,
        status->flow_speed,
        status->flow_brightness,
        flow_soft_mode,
        breathing_enabled,
        status->breathing_speed,
        status->breathing_strength,
        manual_override,
        status->current_lux,
        status->target_lux,
        status->tolerance_lux,
        status->target_lux_min,
        status->target_lux_max,
        status->led_output_percent,
        status->control_state,
        sensor_ok);
}

int slgw_json_build_status_integration(char *out, size_t out_size, const slgw_config *cfg, const slgw_status_frame *status) {
    return slgw_json_build_status(out, out_size, cfg, status, NULL);
}

int slgw_json_build_event(char *out, size_t out_size, const slgw_config *cfg, const slgw_event_frame *event_frame) {
    char ts[40];
    char event_type[32];
    char message[SLGW_ARG_TEXT_SIZE];

    if (out == NULL || out_size == 0 || cfg == NULL || event_frame == NULL) {
        return -1;
    }

    slgw_now_ts_iso(ts, sizeof(ts));
    slgw_copy_lower_text(event_type, sizeof(event_type), event_frame->event_type);
    slgw_copy_lower_text(message, sizeof(message), event_frame->message);

    return slgw_format(
        out,
        out_size,
        "{\"ts\":\"%s\",\"type\":\"event\",\"node_id\":\"%s\",\"event_type\":\"%s\",\"code\":\"%s\",\"message\":\"%s\",\"uptime_ms\":%u}",
        ts,
        cfg->node_id,
        event_type,
        event_frame->code,
        message,
        event_frame->uptime_ms);
}

int slgw_json_build_event_integration(char *out, size_t out_size, const slgw_config *cfg, const slgw_event_frame *event_frame) {
    return slgw_json_build_event(out, out_size, cfg, event_frame);
}

int slgw_json_build_availability_internal(char *out, size_t out_size, const slgw_config *cfg, bool online, const char *reason) {
    (void)cfg;
    (void)reason;
    return slgw_copy_text(out, out_size, online ? "online" : "offline");
}

int slgw_json_build_availability_integration(char *out, size_t out_size, const slgw_config *cfg, bool online, const char *reason) {
    return slgw_json_build_availability_internal(out, out_size, cfg, online, reason);
}
