#include <stdint.h>
#include <string.h>

#include "pico/time.h"

#include "../config.h"
#include "auto_light_service.h"
#include "../utils/color_utils.h"

static void copy_reason(char *dest, size_t dest_size, const char *reason);

static uint8_t pct_to_u8(int pct)
{
    if (pct < BRIGHTNESS_PCT_MIN) pct = BRIGHTNESS_PCT_MIN;
    if (pct > BRIGHTNESS_PCT_MAX) pct = BRIGHTNESS_PCT_MAX;
    return (uint8_t)((pct * 255 + 50) / 100);
}

static int u8_to_pct(uint8_t value)
{
    return ((int)value * 100 + 127) / 255;
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static void set_control_state(auto_light_service_t *service, const char *state)
{
    if (service == NULL || state == NULL)
    {
        return;
    }
    copy_reason(service->control_state, sizeof(service->control_state), state);
}

static int map_zone_from_lux(float lux, int *brightness_pct, int *color_temp_k)
{
    if (lux <= AUTO_LIGHT_ZONE1_MAX_LUX) { *brightness_pct = AUTO_LIGHT_ZONE1_BRIGHTNESS_PCT; *color_temp_k = AUTO_LIGHT_ZONE1_COLOR_TEMP_K; return 0; }
    if (lux <= AUTO_LIGHT_ZONE2_MAX_LUX) { *brightness_pct = AUTO_LIGHT_ZONE2_BRIGHTNESS_PCT; *color_temp_k = AUTO_LIGHT_ZONE2_COLOR_TEMP_K; return 1; }
    if (lux <= AUTO_LIGHT_ZONE3_MAX_LUX) { *brightness_pct = AUTO_LIGHT_ZONE3_BRIGHTNESS_PCT; *color_temp_k = AUTO_LIGHT_ZONE3_COLOR_TEMP_K; return 2; }
    if (lux <= AUTO_LIGHT_ZONE4_MAX_LUX) { *brightness_pct = AUTO_LIGHT_ZONE4_BRIGHTNESS_PCT; *color_temp_k = AUTO_LIGHT_ZONE4_COLOR_TEMP_K; return 3; }
    *brightness_pct = AUTO_LIGHT_ZONE5_BRIGHTNESS_PCT;
    *color_temp_k = AUTO_LIGHT_ZONE5_COLOR_TEMP_K;
    return 4;
}

static void copy_reason(char *dest, size_t dest_size, const char *reason)
{
    if (dest == NULL || dest_size == 0u)
    {
        return;
    }
    if (reason == NULL)
    {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, reason, dest_size - 1u);
    dest[dest_size - 1u] = '\0';
}

static void note_fault(auto_light_service_t *service, const char *reason)
{
    if (service == NULL || reason == NULL)
    {
        return;
    }

    if (!service->fault_active || strcmp(service->last_error_reason, reason) != 0)
    {
        service->pending_sensor_error = true;
        service->pending_fallback_notice = true;
        copy_reason(service->pending_error_reason, sizeof(service->pending_error_reason), reason);
        copy_reason(service->last_error_reason, sizeof(service->last_error_reason), reason);
    }

    service->fault_active = true;
}

static void note_recovery(auto_light_service_t *service)
{
    if (service == NULL)
    {
        return;
    }
    if (service->fault_active)
    {
        service->pending_sensor_recovered = true;
    }
    service->fault_active = false;
    service->last_error_reason[0] = '\0';
}

bool auto_light_service_init(auto_light_service_t *service,
                             light_sensor_service_t *light_service,
                             led_service_t *led_service)
{
    if (service == NULL || light_service == NULL || led_service == NULL)
    {
        return false;
    }
    memset(service, 0, sizeof(*service));
    service->light_service = light_service;
    service->led_service = led_service;
    service->current_zone = -1;
    service->current_brightness_pct = BRIGHTNESS_PCT_MIN;
    service->current_color_temp_k = COLOR_TEMP_K_NONE;
    service->target_lux = -1;
    service->tolerance_lux = -1;
    service->target_lux_min = -1;
    service->target_lux_max = -1;
    service->min_output_pct = 0;
    service->max_output_pct = 100;
    service->output_step_pct = 5;
    service->target_r = 255u;
    service->target_g = 255u;
    service->target_b = 255u;
    service->adaptive_enabled = false;
    service->sensor_ok = false;
    service->last_sensor_read_ms = 0u;
    service->refresh_requested = true;
    set_control_state(service, "off");
    return true;
}

bool auto_light_service_configure_range(auto_light_service_t *service,
                                        int target_lux,
                                        int tolerance_lux,
                                        int min_output_pct,
                                        int max_output_pct,
                                        int color_temp_k,
                                        uint8_t r,
                                        uint8_t g,
                                        uint8_t b,
                                        int output_step_pct)
{
    bool config_changed;

    if (service == NULL || target_lux < 0 || tolerance_lux < 0)
    {
        return false;
    }
    if (color_temp_k != COLOR_TEMP_K_NONE)
    {
        if (!color_utils_kelvin_to_rgb(color_temp_k, &r, &g, &b))
        {
            return false;
        }
    }

    config_changed = (!service->adaptive_enabled) ||
                     (service->target_lux != target_lux) ||
                     (service->tolerance_lux != tolerance_lux) ||
                     (service->min_output_pct != clamp_int(min_output_pct, BRIGHTNESS_PCT_MIN, BRIGHTNESS_PCT_MAX)) ||
                     (service->max_output_pct != clamp_int(max_output_pct, clamp_int(min_output_pct, BRIGHTNESS_PCT_MIN, BRIGHTNESS_PCT_MAX), BRIGHTNESS_PCT_MAX)) ||
                     (service->output_step_pct != clamp_int(output_step_pct, 1, 20)) ||
                     (service->target_r != r) ||
                     (service->target_g != g) ||
                     (service->target_b != b);

    service->target_lux = target_lux;
    service->tolerance_lux = tolerance_lux;
    service->target_lux_min = target_lux - tolerance_lux;
    if (service->target_lux_min < 0)
    {
        service->target_lux_min = 0;
    }
    service->target_lux_max = target_lux + tolerance_lux;
    service->min_output_pct = clamp_int(min_output_pct, BRIGHTNESS_PCT_MIN, BRIGHTNESS_PCT_MAX);
    service->max_output_pct = clamp_int(max_output_pct, service->min_output_pct, BRIGHTNESS_PCT_MAX);
    service->output_step_pct = clamp_int(output_step_pct, 1, 20);
    service->target_r = r;
    service->target_g = g;
    service->target_b = b;
    service->adaptive_enabled = true;

    if (config_changed)
    {
        auto_light_service_request_refresh(service);
    }

    return true;
}

bool auto_light_service_disable(auto_light_service_t *service, const char *state)
{
    if (service == NULL)
    {
        return false;
    }
    service->adaptive_enabled = false;
    service->target_lux = -1;
    service->tolerance_lux = -1;
    service->target_lux_min = -1;
    service->target_lux_max = -1;
    service->refresh_requested = false;
    set_control_state(service, state != NULL ? state : "off");
    return true;
}

bool auto_light_service_update(auto_light_service_t *service)
{
    float raw_lux = 0.0f;
    uint32_t now_ms;

    if (service == NULL || service->light_service == NULL || service->led_service == NULL)
    {
        return false;
    }

    if (!service->adaptive_enabled)
    {
        return led_service_step_transition(service->led_service);
    }

    now_ms = to_ms_since_boot(get_absolute_time());
    if (!service->refresh_requested &&
        service->last_sensor_read_ms != 0u &&
        (now_ms - service->last_sensor_read_ms) < SENSOR_READ_INTERVAL_MS)
    {
        return led_service_step_transition(service->led_service);
    }

    service->refresh_requested = false;
    service->last_sensor_read_ms = now_ms;

    if (!light_sensor_service_read_lux(service->light_service, &raw_lux))
    {
        service->consecutive_failures += 1;
        if (service->consecutive_failures >= 3)
        {
            note_fault(service, "lux_sensor_read_failed");
            service->sensor_ok = false;
            set_control_state(service, "sensor_fault");
        }
        if (service->lux_filter_initialized &&
            service->last_valid_read_ms > 0u &&
            (now_ms - service->last_valid_read_ms) > ADAPTIVE_SENSOR_TIMEOUT_MS)
        {
            note_fault(service, "lux_sensor_timeout");
            service->sensor_ok = false;
            set_control_state(service, "sensor_fault");
        }
        return led_service_step_transition(service->led_service);
    }

    service->consecutive_failures = 0;
    if (raw_lux < 0.0f || raw_lux > AUTO_SENSOR_MAX_LUX)
    {
        note_fault(service, "lux_sensor_out_of_range");
        service->sensor_ok = false;
        set_control_state(service, "sensor_fault");
        return led_service_step_transition(service->led_service);
    }

    note_recovery(service);
    service->last_valid_read_ms = now_ms;
    service->sensor_ok = true;
    if (!service->lux_filter_initialized)
    {
        service->filtered_lux = raw_lux;
        service->lux_filter_initialized = true;
    }
    else
    {
        service->filtered_lux = (AUTO_LIGHT_EMA_ALPHA * raw_lux) + (AUTO_LIGHT_EMA_OLD_WEIGHT * service->filtered_lux);
    }

    if (service->target_lux_min >= 0 && service->target_lux_max >= service->target_lux_min)
    {
        uint8_t led_r = 0u, led_g = 0u, led_b = 0u, led_brightness = 0u;
        int output_pct;
        int next_pct;

        led_service_get_state(service->led_service, &led_r, &led_g, &led_b, &led_brightness);
        output_pct = u8_to_pct(led_brightness);
        next_pct = output_pct;

        if (service->filtered_lux < (float)service->target_lux_min)
        {
            if (output_pct >= service->max_output_pct)
            {
                next_pct = service->max_output_pct;
                set_control_state(service, "max_output_reached");
            }
            else
            {
                next_pct = clamp_int(output_pct + service->output_step_pct, service->min_output_pct, service->max_output_pct);
                set_control_state(service, "below_range");
            }
        }
        else if (service->filtered_lux > (float)service->target_lux_max)
        {
            if (output_pct <= service->min_output_pct)
            {
                next_pct = service->min_output_pct;
                set_control_state(service, "min_output_reached");
            }
            else
            {
                next_pct = clamp_int(output_pct - service->output_step_pct, service->min_output_pct, service->max_output_pct);
                set_control_state(service, "above_range");
            }
        }
        else
        {
            next_pct = clamp_int(output_pct, service->min_output_pct, service->max_output_pct);
            set_control_state(service, "in_range");
        }

        led_service_set_target_color(service->led_service, service->target_r, service->target_g, service->target_b);
        led_service_set_target_brightness(service->led_service, pct_to_u8(next_pct));
        service->current_brightness_pct = next_pct;
        service->current_color_temp_k = COLOR_TEMP_K_NONE;
        service->last_output_lux = service->filtered_lux;
        return led_service_step_transition(service->led_service);
    }
    else
    {
        int brightness_pct = 0;
        int color_temp_k = COLOR_TEMP_K_NONE;
        int zone = map_zone_from_lux(service->filtered_lux, &brightness_pct, &color_temp_k);
        if (zone != service->current_zone &&
            (service->last_switch_ms == 0u || (now_ms - service->last_switch_ms) >= AUTO_MIN_SWITCH_INTERVAL_MS))
        {
            uint8_t r = 0u, g = 0u, b = 0u;
            if (!color_utils_kelvin_to_rgb(color_temp_k, &r, &g, &b))
            {
                return false;
            }
            if (!led_service_set_target_brightness(service->led_service, pct_to_u8(brightness_pct)))
            {
                return false;
            }
            if (!led_service_set_target_color(service->led_service, r, g, b))
            {
                return false;
            }
            service->current_zone = zone;
            service->current_brightness_pct = brightness_pct;
            service->current_color_temp_k = color_temp_k;
            service->last_switch_ms = now_ms;
            service->last_output_lux = service->filtered_lux;
        }
    }

    return led_service_step_transition(service->led_service);
}

bool auto_light_service_get_filtered_lux(const auto_light_service_t *service,
                                         float *lux_value)
{
    if (service == NULL || lux_value == NULL || !service->lux_filter_initialized)
    {
        return false;
    }
    *lux_value = service->filtered_lux;
    return true;
}

bool auto_light_service_get_last_output_lux(const auto_light_service_t *service,
                                            float *lux_value)
{
    if (service == NULL || lux_value == NULL || !service->lux_filter_initialized)
    {
        return false;
    }
    *lux_value = service->last_output_lux;
    return true;
}

bool auto_light_service_get_current_output(const auto_light_service_t *service,
                                           int *brightness_pct,
                                           int *color_temp_k)
{
    if (service == NULL)
    {
        return false;
    }
    if (brightness_pct != NULL)
    {
        *brightness_pct = service->current_brightness_pct;
    }
    if (color_temp_k != NULL)
    {
        *color_temp_k = service->current_color_temp_k;
    }
    return service->current_color_temp_k != COLOR_TEMP_K_NONE;
}

bool auto_light_service_get_control_status(const auto_light_service_t *service,
                                           float *current_lux,
                                           int *target_lux,
                                           int *tolerance_lux,
                                           int *target_lux_min,
                                           int *target_lux_max,
                                           int *led_output_percent,
                                           const char **control_state,
                                           bool *sensor_ok)
{
    uint8_t led_brightness = 0u;
    if (service == NULL)
    {
        return false;
    }
    if (current_lux != NULL)
    {
        *current_lux = service->lux_filter_initialized ? service->filtered_lux : -1.0f;
    }
    if (target_lux != NULL) *target_lux = service->target_lux;
    if (tolerance_lux != NULL) *tolerance_lux = service->tolerance_lux;
    if (target_lux_min != NULL) *target_lux_min = service->target_lux_min;
    if (target_lux_max != NULL) *target_lux_max = service->target_lux_max;
    if (led_output_percent != NULL)
    {
        if (service->led_service != NULL)
        {
            led_service_get_state(service->led_service, NULL, NULL, NULL, &led_brightness);
            *led_output_percent = u8_to_pct(led_brightness);
        }
        else
        {
            *led_output_percent = service->current_brightness_pct;
        }
    }
    if (control_state != NULL) *control_state = service->control_state;
    if (sensor_ok != NULL) *sensor_ok = service->sensor_ok;
    return true;
}

bool auto_light_service_request_refresh(auto_light_service_t *service)
{
    if (service == NULL)
    {
        return false;
    }
    service->current_zone = -1;
    service->last_switch_ms = 0u;
    service->refresh_requested = true;
    return true;
}

bool auto_light_service_consume_error(auto_light_service_t *service,
                                      char *reason,
                                      uint16_t reason_size)
{
    if (service == NULL || !service->pending_sensor_error)
    {
        return false;
    }
    if (reason != NULL && reason_size > 0u)
    {
        copy_reason(reason, reason_size, service->pending_error_reason);
    }
    service->pending_sensor_error = false;
    return true;
}

bool auto_light_service_consume_recovery(auto_light_service_t *service)
{
    if (service == NULL || !service->pending_sensor_recovered)
    {
        return false;
    }
    service->pending_sensor_recovered = false;
    return true;
}

bool auto_light_service_consume_fallback_notice(auto_light_service_t *service,
                                                char *reason,
                                                uint16_t reason_size)
{
    if (service == NULL || !service->pending_fallback_notice)
    {
        return false;
    }
    if (reason != NULL && reason_size > 0u)
    {
        copy_reason(reason, reason_size, service->last_error_reason);
    }
    service->pending_fallback_notice = false;
    return true;
}
