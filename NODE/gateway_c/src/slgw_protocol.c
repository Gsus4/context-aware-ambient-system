#include "slgw_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slgw_text.h"

#define SLGW_PROTOCOL_MAX_PARTS 40
#define SLGW_PROTOCOL_PART_SIZE 96

static int slgw_split_csv(const char *line, char parts[][SLGW_PROTOCOL_PART_SIZE], int max_parts) {
    int count = 0;
    const char *cursor = line;
    if (line == NULL) {
        return -1;
    }
    while (*cursor != '\0') {
        const char *next = strchr(cursor, ',');
        size_t length = next == NULL ? strlen(cursor) : (size_t)(next - cursor);
        if (count >= max_parts) {
            return -1;
        }
        if (length + 1 > SLGW_PROTOCOL_PART_SIZE) {
            return -1;
        }
        memcpy(parts[count], cursor, length);
        parts[count][length] = '\0';
        count += 1;
        if (next == NULL) {
            break;
        }
        cursor = next + 1;
    }
    return count;
}

static bool slgw_text_on(const char *text) {
    return text != NULL && strcmp(text, "ON") == 0;
}

static int slgw_status_copy_mode(char *out, size_t out_size, const char *mode_text) {
    if (mode_text == NULL) {
        return slgw_copy_text(out, out_size, "unknown");
    }
    if (strcmp(mode_text, "MANUAL") == 0 || strcmp(mode_text, "STATIC") == 0) {
        return slgw_copy_text(out, out_size, "static");
    }
    if (strcmp(mode_text, "SCENE") == 0) {
        return slgw_copy_text(out, out_size, "scene");
    }
    if (strcmp(mode_text, "AUTO") == 0) {
        return slgw_copy_text(out, out_size, "auto");
    }
    if (strcmp(mode_text, "FLOW") == 0) {
        return slgw_copy_text(out, out_size, "flow");
    }
    return slgw_copy_lower_text(out, out_size, mode_text);
}

int slgw_protocol_clamp_u8(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

int slgw_protocol_build_cmd_line(char *out, size_t out_size, const char *req_id, const char *command, const char *arg_text) {
    if (req_id == NULL || req_id[0] == '\0' || command == NULL || command[0] == '\0') {
        return -1;
    }
    if (arg_text != NULL && arg_text[0] != '\0') {
        return slgw_format(out, out_size, "CMD,%s,%s,%s", req_id, command, arg_text);
    }
    return slgw_format(out, out_size, "CMD,%s,%s", req_id, command);
}

bool slgw_protocol_parse_resp(const char *line, slgw_resp_frame *out) {
    char parts[SLGW_PROTOCOL_MAX_PARTS][SLGW_PROTOCOL_PART_SIZE];
    int count;
    int idx;
    memset(out, 0, sizeof(*out));
    if (slgw_copy_text(out->raw, sizeof(out->raw), line) != 0) {
        return false;
    }
    count = slgw_split_csv(line, parts, SLGW_PROTOCOL_MAX_PARTS);
    if (count < 5 || strcmp(parts[0], "RESP") != 0) {
        return false;
    }
    if (slgw_copy_text(out->req_id, sizeof(out->req_id), parts[1]) != 0) return false;
    if (slgw_copy_text(out->status, sizeof(out->status), parts[2]) != 0) return false;
    if (slgw_copy_text(out->command, sizeof(out->command), parts[3]) != 0) return false;
    if (strcmp(out->status, "OK") == 0) {
        if (count < 5 || slgw_parse_unsigned(parts[4], &out->uptime_ms) != 0) {
            return false;
        }
        out->arg_count = 0;
        return true;
    }
    if (count < 7) {
        return false;
    }
    for (idx = 4; idx < count - 1 && out->arg_count < SLGW_ARGS_MAX; ++idx) {
        if (slgw_copy_text(out->args[out->arg_count], sizeof(out->args[out->arg_count]), parts[idx]) != 0) {
            return false;
        }
        out->arg_count += 1;
    }
    if (slgw_parse_unsigned(parts[count - 1], &out->uptime_ms) != 0) {
        return false;
    }
    return true;
}

bool slgw_protocol_parse_event(const char *line, slgw_event_frame *out) {
    char parts[SLGW_PROTOCOL_MAX_PARTS][SLGW_PROTOCOL_PART_SIZE];
    int count;
    memset(out, 0, sizeof(*out));
    if (slgw_copy_text(out->raw, sizeof(out->raw), line) != 0) {
        return false;
    }
    count = slgw_split_csv(line, parts, SLGW_PROTOCOL_MAX_PARTS);
    if (count < 5 || strcmp(parts[0], "EVENT") != 0) {
        return false;
    }
    if (slgw_copy_text(out->event_type, sizeof(out->event_type), parts[1]) != 0) return false;
    if (slgw_copy_text(out->code, sizeof(out->code), parts[2]) != 0) return false;
    if (count == 5) {
        if (slgw_copy_text(out->message, sizeof(out->message), parts[3]) != 0) return false;
    } else {
        size_t cursor = 0;
        int idx;
        out->message[0] = '\0';
        for (idx = 3; idx < count - 1; ++idx) {
            int rc = snprintf(out->message + cursor, sizeof(out->message) - cursor, "%s%s", idx == 3 ? "" : ",", parts[idx]);
            if (rc < 0 || (size_t)rc >= sizeof(out->message) - cursor) return false;
            cursor += (size_t)rc;
        }
    }
    if (slgw_parse_unsigned(parts[count - 1], &out->uptime_ms) != 0) return false;
    out->valid = true;
    return true;
}

static bool slgw_protocol_parse_status_v3(char parts[][SLGW_PROTOCOL_PART_SIZE], int count, slgw_status_frame *out) {
    if (count < 18) {
        return false;
    }
    if (slgw_copy_text(out->protocol_version, sizeof(out->protocol_version), parts[2]) != 0) return false;
    if (slgw_parse_unsigned(parts[5], &out->uptime_ms) != 0) return false;
    if (slgw_status_copy_mode(out->active_mode, sizeof(out->active_mode), parts[8]) != 0) return false;
    if (slgw_copy_lower_text(out->active_scene, sizeof(out->active_scene), parts[9]) != 0) return false;
    out->scene_modified = false;
    if (slgw_parse_int(parts[13], &out->brightness_pct) != 0) return false;
    out->tone_bias = 0;
    out->flow_enabled = slgw_text_on(parts[15]);
    if (slgw_copy_lower_text(out->flow_preset, sizeof(out->flow_preset), parts[16]) != 0) return false;
    if (slgw_parse_int(parts[17], &out->flow_brightness) != 0) return false;
    if (slgw_copy_text(out->flow_speed, sizeof(out->flow_speed), out->flow_enabled ? "medium" : "slow") != 0) return false;
    out->flow_soft_mode = false;
    out->breathing_enabled = false;
    if (slgw_copy_text(out->breathing_speed, sizeof(out->breathing_speed), "slow") != 0) return false;
    if (slgw_copy_text(out->breathing_strength, sizeof(out->breathing_strength), "low") != 0) return false;
    out->manual_override = false;
    out->valid = true;
    return true;
}

static bool slgw_protocol_parse_status_v4(char parts[][SLGW_PROTOCOL_PART_SIZE], int count, slgw_status_frame *out) {
    if (count != 20) {
        return false;
    }
    if (slgw_copy_text(out->protocol_version, sizeof(out->protocol_version), parts[2]) != 0) return false;
    if (slgw_parse_unsigned(parts[3], &out->uptime_ms) != 0) return false;
    if (slgw_copy_lower_text(out->active_mode, sizeof(out->active_mode), parts[4]) != 0) return false;
    if (slgw_copy_lower_text(out->active_scene, sizeof(out->active_scene), parts[5]) != 0) return false;
    out->scene_modified = slgw_text_on(parts[6]);
    if (slgw_parse_int(parts[7], &out->brightness_pct) != 0) return false;
    if (slgw_parse_int(parts[8], &out->tone_bias) != 0) return false;
    out->flow_enabled = slgw_text_on(parts[9]);
    if (slgw_copy_lower_text(out->flow_preset, sizeof(out->flow_preset), parts[10]) != 0) return false;
    if (slgw_copy_lower_text(out->flow_speed, sizeof(out->flow_speed), parts[11]) != 0) return false;
    if (slgw_parse_int(parts[12], &out->flow_brightness) != 0) return false;
    out->flow_soft_mode = slgw_text_on(parts[13]);
    out->breathing_enabled = slgw_text_on(parts[14]);
    if (slgw_copy_lower_text(out->breathing_speed, sizeof(out->breathing_speed), parts[15]) != 0) return false;
    if (slgw_copy_lower_text(out->breathing_strength, sizeof(out->breathing_strength), parts[16]) != 0) return false;
    out->manual_override = slgw_text_on(parts[19]);
    out->valid = true;
    return true;
}

static bool slgw_protocol_parse_status_v5(char parts[][SLGW_PROTOCOL_PART_SIZE], int count, slgw_status_frame *out) {
    int offset = 0;
    if (count != 18 && count != 19) {
        return false;
    }
    if (slgw_copy_text(out->protocol_version, sizeof(out->protocol_version), parts[2]) != 0) return false;
    if (slgw_parse_unsigned(parts[3], &out->uptime_ms) != 0) return false;
    if (slgw_copy_lower_text(out->active_mode, sizeof(out->active_mode), parts[4]) != 0) return false;
    if (slgw_copy_lower_text(out->active_scene, sizeof(out->active_scene), parts[5]) != 0) return false;
    out->scene_modified = slgw_text_on(parts[6]);
    if (slgw_parse_int(parts[7], &out->brightness_pct) != 0) return false;
    if (slgw_parse_int(parts[8], &out->tone_bias) != 0) return false;
    if (count == 19) {
        if (slgw_parse_int(parts[9], &out->color_temp_k) != 0) return false;
        offset = 1;
    } else {
        out->color_temp_k = 0;
    }
    out->flow_enabled = slgw_text_on(parts[9 + offset]);
    if (slgw_copy_lower_text(out->flow_preset, sizeof(out->flow_preset), parts[10 + offset]) != 0) return false;
    if (slgw_copy_lower_text(out->flow_speed, sizeof(out->flow_speed), parts[11 + offset]) != 0) return false;
    if (slgw_parse_int(parts[12 + offset], &out->flow_brightness) != 0) return false;
    out->flow_soft_mode = slgw_text_on(parts[13 + offset]);
    out->breathing_enabled = slgw_text_on(parts[14 + offset]);
    if (slgw_copy_lower_text(out->breathing_speed, sizeof(out->breathing_speed), parts[15 + offset]) != 0) return false;
    if (slgw_copy_lower_text(out->breathing_strength, sizeof(out->breathing_strength), parts[16 + offset]) != 0) return false;
    out->manual_override = slgw_text_on(parts[17 + offset]);
    out->valid = true;
    return true;
}

static bool slgw_protocol_parse_status_v6(char parts[][SLGW_PROTOCOL_PART_SIZE], int count, slgw_status_frame *out) {
    char *endptr = NULL;
    if (count != 28) {
        return false;
    }
    if (slgw_copy_text(out->protocol_version, sizeof(out->protocol_version), parts[2]) != 0) return false;
    if (slgw_parse_unsigned(parts[3], &out->uptime_ms) != 0) return false;
    if (slgw_copy_lower_text(out->active_mode, sizeof(out->active_mode), parts[4]) != 0) return false;
    if (slgw_copy_lower_text(out->active_scene, sizeof(out->active_scene), parts[5]) != 0) return false;
    out->scene_modified = slgw_text_on(parts[6]);
    if (slgw_parse_int(parts[7], &out->brightness_pct) != 0) return false;
    if (slgw_parse_int(parts[8], &out->tone_bias) != 0) return false;
    if (slgw_parse_int(parts[9], &out->color_temp_k) != 0) return false;
    out->flow_enabled = slgw_text_on(parts[10]);
    if (slgw_copy_lower_text(out->flow_preset, sizeof(out->flow_preset), parts[11]) != 0) return false;
    if (slgw_copy_lower_text(out->flow_speed, sizeof(out->flow_speed), parts[12]) != 0) return false;
    if (slgw_parse_int(parts[13], &out->flow_brightness) != 0) return false;
    out->flow_soft_mode = slgw_text_on(parts[14]);
    out->breathing_enabled = slgw_text_on(parts[15]);
    if (slgw_copy_lower_text(out->breathing_speed, sizeof(out->breathing_speed), parts[16]) != 0) return false;
    if (slgw_copy_lower_text(out->breathing_strength, sizeof(out->breathing_strength), parts[17]) != 0) return false;
    out->manual_override = slgw_text_on(parts[18]);
    out->current_lux = strtod(parts[19], &endptr);
    if (endptr == parts[19] || *endptr != '\0') return false;
    if (slgw_parse_int(parts[20], &out->target_lux) != 0) return false;
    if (slgw_parse_int(parts[21], &out->tolerance_lux) != 0) return false;
    if (slgw_parse_int(parts[22], &out->target_lux_min) != 0) return false;
    if (slgw_parse_int(parts[23], &out->target_lux_max) != 0) return false;
    if (slgw_parse_int(parts[24], &out->led_output_percent) != 0) return false;
    if (slgw_copy_lower_text(out->control_state, sizeof(out->control_state), parts[25]) != 0) return false;
    out->sensor_ok = slgw_text_on(parts[26]);
    if (slgw_copy_lower_text(out->custom_control_type, sizeof(out->custom_control_type), parts[27]) != 0) return false;
    out->valid = true;
    return true;
}

bool slgw_protocol_parse_status(const char *line, slgw_status_frame *out) {
    char parts[SLGW_PROTOCOL_MAX_PARTS][SLGW_PROTOCOL_PART_SIZE];
    int count;
    memset(out, 0, sizeof(*out));
    count = slgw_split_csv(line, parts, SLGW_PROTOCOL_MAX_PARTS);
    if (count < 10) return false;
    if (strcmp(parts[0], "STATE") != 0 || strcmp(parts[1], "STATUS") != 0) return false;
    if (strcmp(parts[2], "V6") == 0) return slgw_protocol_parse_status_v6(parts, count, out);
    if (strcmp(parts[2], "V5") == 0) return slgw_protocol_parse_status_v5(parts, count, out);
    if (strcmp(parts[2], "V4") == 0) return slgw_protocol_parse_status_v4(parts, count, out);
    if (strcmp(parts[2], "V3") == 0) return slgw_protocol_parse_status_v3(parts, count, out);
    return false;
}
