#include <stdio.h>
#include <string.h>

#include "pico/time.h"

#include "logger.h"
#include "status_report.h"
#include "config.h"

static uint32_t g_status_report_session_id = 0u;
static system_state_t g_status_report_system_state = SYSTEM_STATE_INIT;
static bool g_status_report_report_enabled = false;

void status_report_init(uint32_t session_id)
{
    g_status_report_session_id = session_id;
    g_status_report_system_state = SYSTEM_STATE_INIT;
    g_status_report_report_enabled = false;
}

void status_report_set_system_state(system_state_t system_state)
{
    g_status_report_system_state = system_state;
}

void status_report_set_report_enabled(bool enabled)
{
    g_status_report_report_enabled = enabled;
}

uint32_t status_report_get_session_id(void)
{
    return g_status_report_session_id;
}

static int pct_from_u8(uint8_t value)
{
    return ((int)value * 100 + 127) / 255;
}

void status_report_build_v5(char *buffer,
                            uint16_t buffer_size,
                            mode_service_t *mode_service,
                            light_sensor_service_t *light_service)
{
    light_mode_t mode;
    int scene_brightness_pct = 0;
    int tone_bias = 0;
    bool scene_modified = false;
    int color_temp_k = COLOR_TEMP_K_NONE;
    int manual_color_temp_k = COLOR_TEMP_K_NONE;
    bool breathing_enabled = false;
    breathing_speed_t breathing_speed = BREATHING_SPEED_SLOW;
    breathing_strength_t breathing_strength = BREATHING_STRENGTH_LOW;
    uint8_t manual_r = 0u, manual_g = 0u, manual_b = 0u, manual_brightness_pct = 0u;
    flow_preset_t flow_preset_value = FLOW_PRESET_WARM_AMBIENT;
    const char *active_scene = "NONE";
    const char *flow_preset = "NONE";
    const char *flow_speed = "SLOW";
    int brightness_pct = 0;
    int flow_brightness_pct = 0;
    bool flow_enabled = false;
    bool flow_soft_mode = false;
    bool manual_override = false;
    float current_lux = -1.0f;
    int target_lux = -1;
    int tolerance_lux = -1;
    int target_lux_min = -1;
    int target_lux_max = -1;
    int led_output_percent = 0;
    const char *control_state = "off";
    bool sensor_ok = false;
    const char *custom_control_type = "fixed_brightness";
    (void)light_service;
    (void)g_status_report_system_state;
    (void)g_status_report_report_enabled;

    if (!buffer || buffer_size == 0 || !mode_service || !light_service)
    {
        log_error("status_report: invalid argument");
        return;
    }

    buffer[0] = '\0';
    mode = mode_service_get_mode(mode_service);
    mode_service_get_control_status(mode_service,
                                    &current_lux,
                                    &target_lux,
                                    &tolerance_lux,
                                    &target_lux_min,
                                    &target_lux_max,
                                    &led_output_percent,
                                    &control_state,
                                    &sensor_ok,
                                    &custom_control_type);
    mode_service_get_scene_state(mode_service, &scene_brightness_pct, &tone_bias, &color_temp_k, &scene_modified);
    if (mode != LIGHT_MODE_SCENE)
    {
        scene_modified = false;
        tone_bias = 0;
        color_temp_k = COLOR_TEMP_K_NONE;
    }
    mode_service_get_breathing(mode_service, &breathing_enabled, &breathing_speed, &breathing_strength);
    mode_service_get_manual_state(mode_service, &manual_r, &manual_g, &manual_b, &manual_brightness_pct, &manual_color_temp_k);
    manual_override = mode_service_get_manual_override(mode_service);

    if (mode_service->flow_service != NULL)
    {
        flow_preset_value = flow_service_get_preset(mode_service->flow_service);
        flow_preset = flow_preset_to_string(flow_preset_value);
        flow_speed = flow_service_speed_to_string(flow_service_get_speed(mode_service->flow_service));
        flow_brightness_pct = pct_from_u8(flow_service_get_brightness(mode_service->flow_service));
        flow_soft_mode = flow_service_get_soft_mode(mode_service->flow_service);
    }

    if (!mode_service->powered_on)
    {
        brightness_pct = 0;
        flow_brightness_pct = 0;
        flow_enabled = false;
        breathing_enabled = false;

        if (mode == LIGHT_MODE_SCENE)
        {
            active_scene = mode_service_scene_to_string(mode_service_get_scene(mode_service));
        }
        else
        {
            active_scene = "NONE";
        }
    }
    else if (mode == LIGHT_MODE_SCENE)
    {
        brightness_pct = scene_brightness_pct;
        flow_brightness_pct = 0;
        flow_enabled = false;
        active_scene = mode_service_scene_to_string(mode_service_get_scene(mode_service));
    }
    else if (mode == LIGHT_MODE_MANUAL)
    {
        brightness_pct = (int)manual_brightness_pct;
        color_temp_k = manual_color_temp_k;
        flow_brightness_pct = 0;
        flow_enabled = false;
        active_scene = "NONE";
    }
    else
    {
        brightness_pct = flow_brightness_pct;
        color_temp_k = COLOR_TEMP_K_NONE;
        flow_enabled = true;
        active_scene = "NONE";
    }

    if (!flow_enabled)
    {
        flow_preset = "NONE";
        flow_speed = "SLOW";
        flow_brightness_pct = 0;
        flow_soft_mode = false;
    }

    if (flow_soft_mode)
    {
        breathing_enabled = true;
        breathing_speed = BREATHING_SPEED_SLOW;
        breathing_strength = BREATHING_STRENGTH_LOW;
    }
    else if (mode != LIGHT_MODE_MANUAL)
    {
        breathing_enabled = false;
    }

    if (mode == LIGHT_MODE_SCENE || (mode == LIGHT_MODE_MANUAL && strcmp(custom_control_type, "target_lux_range") == 0))
    {
        brightness_pct = led_output_percent;
    }

    snprintf(buffer,
             buffer_size,
             "STATE,STATUS,V6,%llu,%s,%s,%s,%d,%d,%d,%s,%s,%s,%d,%s,%s,%s,%s,%s,%.1f,%d,%d,%d,%d,%d,%s,%s,%s",
             (unsigned long long)to_ms_since_boot(get_absolute_time()),
             light_mode_to_string(mode),
             active_scene,
             scene_modified ? "ON" : "OFF",
             brightness_pct,
             tone_bias,
             color_temp_k,
             flow_enabled ? "ON" : "OFF",
             flow_preset,
             flow_speed,
             flow_brightness_pct,
             flow_soft_mode ? "ON" : "OFF",
             breathing_enabled ? "ON" : "OFF",
             mode_service_breathing_speed_to_string(breathing_speed),
             mode_service_breathing_strength_to_string(breathing_strength),
             manual_override ? "ON" : "OFF",
             current_lux,
             target_lux,
             tolerance_lux,
             target_lux_min,
             target_lux_max,
             led_output_percent,
             control_state,
             sensor_ok ? "ON" : "OFF",
             custom_control_type);
}

void build_status_report(char *buffer,
                         uint16_t buffer_size,
                         mode_service_t *mode_service,
                         light_sensor_service_t *light_service)
{
    status_report_build_v5(buffer, buffer_size, mode_service, light_service);
}
