#include "mode_service.h"

#include <stddef.h>

#include "pico/time.h"

#include "../config.h"
#include "../utils/color_utils.h"

typedef bool (*mode_update_handler_t)(mode_service_t *service);

typedef struct
{
    light_mode_t mode;
    mode_update_handler_t update_handler;
} mode_entry_t;

static bool mode_service_is_valid_scene(light_scene_t scene);

#define MODE_SCENE_COUNT 5u
#define MODE_BREATH_PHASE_STEP 2u
static const mode_scene_config_t g_scene_table[MODE_SCENE_COUNT] =
{
    {0u, 0u, 0u, 0u, COLOR_TEMP_K_NONE, -1, -1, 0, 0, 1, MODE_COLOR_OFF, false},
    {255u, 180u, 96u, 0u, SCENE_SLEEP_COLOR_TEMP_K, SCENE_SLEEP_TARGET_LUX, SCENE_SLEEP_TOLERANCE_LUX, SCENE_SLEEP_MIN_OUTPUT_PCT, SCENE_SLEEP_MAX_OUTPUT_PCT, 2, MODE_COLOR_CCT, true},
    {255u, 180u, 96u, 5u, SCENE_RELAX_COLOR_TEMP_K, SCENE_RELAX_TARGET_LUX, SCENE_RELAX_TOLERANCE_LUX, SCENE_RELAX_MIN_OUTPUT_PCT, SCENE_RELAX_MAX_OUTPUT_PCT, 3, MODE_COLOR_CCT, true},
    {220u, 235u, 255u, 0u, SCENE_WORK_COLOR_TEMP_K, SCENE_WORK_TARGET_LUX, SCENE_WORK_TOLERANCE_LUX, SCENE_WORK_MIN_OUTPUT_PCT, SCENE_WORK_MAX_OUTPUT_PCT, 5, MODE_COLOR_CCT, true},
    {SCENE_EXERCISE_R, SCENE_EXERCISE_G, SCENE_EXERCISE_B, 10u, COLOR_TEMP_K_NONE, SCENE_EXERCISE_TARGET_LUX, SCENE_EXERCISE_TOLERANCE_LUX, SCENE_EXERCISE_MIN_OUTPUT_PCT, SCENE_EXERCISE_MAX_OUTPUT_PCT, 2, MODE_COLOR_RGB, true}
};

static int mode_service_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static uint8_t mode_service_pct_to_u8(int brightness_pct)
{
    int clamped_pct = mode_service_clamp_int(brightness_pct, BRIGHTNESS_PCT_MIN, BRIGHTNESS_PCT_MAX);
    return (uint8_t)((clamped_pct * 255) / 100);
}

static int mode_service_u8_to_pct(uint8_t value)
{
    return ((int)value * 100 + 127) / 255;
}

static void mode_service_clear_scene_adjustments(mode_service_t *service)
{
    if (service == NULL)
    {
        return;
    }

    service->scene_brightness_pct = -1;
    service->tone_bias = 0;
    service->scene_color_temp_k = -1;
    service->scene_modified = false;
}

static uint32_t mode_service_breathing_interval_ms(breathing_speed_t speed)
{
    switch (speed)
    {
        case BREATHING_SPEED_FAST:   return 15u;
        case BREATHING_SPEED_MEDIUM: return 25u;
        case BREATHING_SPEED_SLOW:
        default:                     return 40u;
    }
}

static int mode_service_breathing_min_pct(breathing_strength_t strength)
{
    switch (strength)
    {
        case BREATHING_STRENGTH_HIGH:   return 20;
        case BREATHING_STRENGTH_MEDIUM: return 45;
        case BREATHING_STRENGTH_LOW:
        default:                        return 70;
    }
}

static uint8_t mode_service_breathing_curve_u8(uint8_t phase)
{
    uint32_t triangle;
    uint32_t x;
    uint32_t y;

    triangle = (phase <= 50u) ? ((uint32_t)phase * 2u) : ((uint32_t)(100u - phase) * 2u);
    if (triangle > 100u)
    {
        triangle = 100u;
    }

    x = (triangle * 255u) / 100u;
    y = (x * x * (765u - (2u * x))) / 65025u;
    if (y > 255u)
    {
        y = 255u;
    }

    return (uint8_t)y;
}

static uint8_t mode_service_apply_breathing(mode_service_t *service, uint8_t base_brightness)
{
    uint32_t now_ms;
    int max_brightness_pct;
    int min_brightness_pct;
    int range_pct;
    uint8_t curve;
    int brightness_pct;

    if (service == NULL || !service->breathing_enabled)
    {
        return base_brightness;
    }

    now_ms = to_ms_since_boot(get_absolute_time());
    if ((now_ms - service->breathing_last_step_ms) >= mode_service_breathing_interval_ms(service->breathing_speed))
    {
        service->breathing_last_step_ms = now_ms;
        if (service->breathing_forward)
        {
            if (service->breathing_phase >= 100u)
            {
                service->breathing_phase = 100u;
                service->breathing_forward = false;
            }
            else
            {
                service->breathing_phase = (uint8_t)(service->breathing_phase + MODE_BREATH_PHASE_STEP);
                if (service->breathing_phase > 100u)
                {
                    service->breathing_phase = 100u;
                }
            }
        }
        else
        {
            if (service->breathing_phase <= MODE_BREATH_PHASE_STEP)
            {
                service->breathing_phase = 0u;
                service->breathing_forward = true;
            }
            else
            {
                service->breathing_phase = (uint8_t)(service->breathing_phase - MODE_BREATH_PHASE_STEP);
            }
        }
    }

    max_brightness_pct = mode_service_u8_to_pct(base_brightness);
    max_brightness_pct = mode_service_clamp_int(max_brightness_pct, 0, 100);
    min_brightness_pct = (max_brightness_pct * mode_service_breathing_min_pct(service->breathing_strength)) / 100;
    min_brightness_pct = mode_service_clamp_int(min_brightness_pct, 0, max_brightness_pct);
    range_pct = max_brightness_pct - min_brightness_pct;
    curve = mode_service_breathing_curve_u8(service->breathing_phase);
    brightness_pct = min_brightness_pct + (range_pct * (int)curve + 127) / 255;
    brightness_pct = mode_service_clamp_int(brightness_pct, 0, 100);

    return mode_service_pct_to_u8(brightness_pct);
}

static bool mode_service_apply_target(led_service_t *led_service,
                                      uint8_t brightness,
                                      uint8_t r,
                                      uint8_t g,
                                      uint8_t b,
                                      bool use_transition)
{
    if (led_service == NULL)
    {
        return false;
    }

    if (!led_service_set_target_brightness(led_service, brightness))
    {
        return false;
    }
    if (!led_service_set_target_color(led_service, r, g, b))
    {
        return false;
    }

    if (use_transition)
    {
        return led_service_step_transition(led_service);
    }

    return led_service_sync_to_target(led_service);
}

static bool mode_service_build_scene_output(const mode_service_t *service,
                                            uint8_t *r,
                                            uint8_t *g,
                                            uint8_t *b,
                                            uint8_t *brightness)
{
    mode_scene_config_t config;
    int brightness_pct;

    if (service == NULL || r == NULL || g == NULL || b == NULL || brightness == NULL)
    {
        return false;
    }
    if ((unsigned int)service->scene >= MODE_SCENE_COUNT)
    {
        return false;
    }

    config = g_scene_table[service->scene];
    *r = config.r;
    *g = config.g;
    *b = config.b;
    if (service->scene_color_temp_k >= 0)
    {
        if (!color_utils_kelvin_to_rgb(service->scene_color_temp_k, r, g, b))
        {
            return false;
        }
    }
    else
    {
        color_utils_apply_tone_bias(service->tone_bias, r, g, b);
    }

    brightness_pct = service->scene_brightness_pct;
    if (brightness_pct < 0)
    {
        brightness_pct = (int)config.brightness_pct;
    }
    *brightness = mode_service_pct_to_u8(brightness_pct);
    return true;
}

static bool mode_service_configure_scene_adaptive(mode_service_t *service)
{
    const mode_scene_config_t *config;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if (service == NULL || service->auto_service == NULL || !mode_service_is_valid_scene(service->scene))
    {
        return false;
    }

    config = &g_scene_table[service->scene];
    if (!config->adaptive_enabled)
    {
        auto_light_service_disable(service->auto_service, "off");
        led_service_off(service->led_service);
        return true;
    }

    r = config->r;
    g = config->g;
    b = config->b;
    return auto_light_service_configure_range(service->auto_service,
                                              config->target_lux,
                                              config->tolerance_lux,
                                              config->min_output_pct,
                                              config->max_output_pct,
                                              config->color_type == MODE_COLOR_CCT ? config->color_temp_k : COLOR_TEMP_K_NONE,
                                              r,
                                              g,
                                              b,
                                              config->output_step_pct);
}

static bool mode_service_note_manual_override(mode_service_t *service)
{
    if (service == NULL)
    {
        return false;
    }

    if (service->mode == LIGHT_MODE_AUTO || service->mode == LIGHT_MODE_SCENE)
    {
        service->manual_override = true;
    }
    return true;
}

static void mode_service_capture_led_as_manual(mode_service_t *service)
{
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    uint8_t brightness = 0u;

    if (service == NULL || service->led_service == NULL)
    {
        return;
    }

    led_service_get_state(service->led_service, &r, &g, &b, &brightness);
    service->manual_r = r;
    service->manual_g = g;
    service->manual_b = b;
    service->manual_brightness_pct = (uint8_t)mode_service_u8_to_pct(brightness);
}

const char *mode_service_mode_to_string(light_mode_t mode)
{
    switch (mode)
    {
        case LIGHT_MODE_AUTO:   return "SCENE";
        case LIGHT_MODE_SCENE:  return "SCENE";
        case LIGHT_MODE_MANUAL: return "CUSTOM";
        case LIGHT_MODE_FLOW:   return "CUSTOM";
        default:                return "UNKNOWN";
    }
}

const char *mode_service_scene_to_string(light_scene_t scene)
{
    switch (scene)
    {
        case LIGHT_SCENE_VACANT:   return "VACANT";
        case LIGHT_SCENE_SLEEP:    return "SLEEP";
        case LIGHT_SCENE_RELAX:    return "RELAX";
        case LIGHT_SCENE_WORK:     return "WORK";
        case LIGHT_SCENE_EXERCISE: return "EXERCISE";
        default:                  return "UNKNOWN";
    }
}

const char *mode_service_custom_control_type_to_string(custom_control_type_t type)
{
    switch (type)
    {
        case CUSTOM_CONTROL_TARGET_LUX_RANGE: return "target_lux_range";
        case CUSTOM_CONTROL_FIXED_BRIGHTNESS:
        default:                              return "fixed_brightness";
    }
}

const char *mode_service_breathing_speed_to_string(breathing_speed_t speed)
{
    switch (speed)
    {
        case BREATHING_SPEED_FAST:   return "FAST";
        case BREATHING_SPEED_MEDIUM: return "MEDIUM";
        case BREATHING_SPEED_SLOW:
        default:                     return "SLOW";
    }
}

const char *mode_service_breathing_strength_to_string(breathing_strength_t strength)
{
    switch (strength)
    {
        case BREATHING_STRENGTH_HIGH:   return "HIGH";
        case BREATHING_STRENGTH_MEDIUM: return "MEDIUM";
        case BREATHING_STRENGTH_LOW:
        default:                        return "LOW";
    }
}

const char *light_mode_to_string(light_mode_t mode)
{
    return mode_service_mode_to_string(mode);
}

const char *scene_mode_to_string(light_scene_t scene)
{
    return mode_service_scene_to_string(scene);
}

static bool mode_service_is_valid_mode(light_mode_t mode)
{
    return (mode == LIGHT_MODE_AUTO) ||
           (mode == LIGHT_MODE_SCENE) ||
           (mode == LIGHT_MODE_MANUAL) ||
           (mode == LIGHT_MODE_FLOW);
}

static bool mode_service_is_valid_scene(light_scene_t scene)
{
    return ((unsigned int)scene < MODE_SCENE_COUNT);
}

static bool mode_update_auto(mode_service_t *service)
{
    if (service == NULL)
    {
        return false;
    }
    return mode_service_set_scene(service, LIGHT_SCENE_WORK);
}

static bool mode_update_scene(mode_service_t *service)
{
    if (service == NULL || service->auto_service == NULL)
    {
        return false;
    }
    if (!service->powered_on)
    {
        led_service_off(service->led_service);
        return true;
    }
    if (!mode_service_configure_scene_adaptive(service))
    {
        return false;
    }
    return auto_light_service_update(service->auto_service);
}

static bool mode_update_manual(mode_service_t *service)
{
    if (service != NULL && service->custom_control_type == CUSTOM_CONTROL_TARGET_LUX_RANGE && service->auto_service != NULL)
    {
        return auto_light_service_update(service->auto_service);
    }
    return mode_service_apply_manual(service);
}

static bool mode_update_flow(mode_service_t *service)
{
    if (service == NULL || service->flow_service == NULL)
    {
        return false;
    }
    return flow_service_update(service->flow_service);
}

static const mode_entry_t g_mode_table[] =
{
    {LIGHT_MODE_AUTO, mode_update_auto},
    {LIGHT_MODE_SCENE, mode_update_scene},
    {LIGHT_MODE_MANUAL, mode_update_manual},
    {LIGHT_MODE_FLOW, mode_update_flow}
};

bool mode_service_apply_manual(mode_service_t *service)
{
    uint8_t effective_brightness;

    if (service == NULL || service->led_service == NULL)
    {
        return false;
    }

    if (!service->powered_on)
    {
        led_service_off(service->led_service);
        return true;
    }

    effective_brightness = mode_service_pct_to_u8(service->manual_brightness_pct);
    effective_brightness = mode_service_apply_breathing(service, effective_brightness);

    return mode_service_apply_target(service->led_service,
                                     effective_brightness,
                                     service->manual_r,
                                     service->manual_g,
                                     service->manual_b,
                                     (!service->breathing_enabled));
}

bool mode_service_apply_scene(mode_service_t *service)
{
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;
    uint8_t brightness = 0u;

    if (service == NULL || service->led_service == NULL)
    {
        return false;
    }

    if (!service->powered_on)
    {
        led_service_off(service->led_service);
        return true;
    }

    if (!mode_service_configure_scene_adaptive(service))
    {
        return false;
    }
    if (service->auto_service != NULL && service->auto_service->adaptive_enabled)
    {
        return auto_light_service_update(service->auto_service);
    }

    if (!mode_service_build_scene_output(service, &r, &g, &b, &brightness))
    {
        return false;
    }

    return mode_service_apply_target(service->led_service,
                                     brightness,
                                     r,
                                     g,
                                     b,
                                     true);
}

bool mode_service_init(mode_service_t *service,
                       auto_light_service_t *auto_service,
                       led_service_t *led_service,
                       flow_service_t *flow_service)
{
    if (service == NULL || auto_service == NULL || led_service == NULL || flow_service == NULL)
    {
        return false;
    }

    service->mode = LIGHT_MODE_SCENE;
    service->scene = LIGHT_SCENE_WORK;
    service->scene_modified = false;
    service->scene_brightness_pct = -1;
    service->tone_bias = 0;
    service->scene_color_temp_k = -1;
    service->manual_color_temp_k = COLOR_TEMP_K_NONE;
    service->manual_override = false;
    service->breathing_enabled = false;
    service->breathing_speed = BREATHING_SPEED_SLOW;
    service->breathing_strength = BREATHING_STRENGTH_LOW;
    service->powered_on = true;

    service->auto_service = auto_service;
    service->led_service = led_service;
    service->flow_service = flow_service;

    service->manual_r = 255u;
    service->manual_g = 255u;
    service->manual_b = 255u;
    service->manual_brightness_pct = (uint8_t)mode_service_u8_to_pct(led_service_get_brightness(led_service));
    service->custom_control_type = CUSTOM_CONTROL_FIXED_BRIGHTNESS;
    service->custom_target_lux = -1;
    service->custom_tolerance_lux = -1;
    service->custom_min_output_pct = 0;
    service->custom_max_output_pct = 100;
    service->breathing_phase = 50u;
    service->breathing_forward = true;
    service->breathing_last_step_ms = 0u;
    return true;
}

bool mode_service_update(mode_service_t *service)
{
    size_t i;

    if (service == NULL)
    {
        return false;
    }

    if (!service->powered_on)
    {
        led_service_off(service->led_service);
        return true;
    }

    for (i = 0u; i < (sizeof(g_mode_table) / sizeof(g_mode_table[0])); i++)
    {
        if (g_mode_table[i].mode == service->mode)
        {
            return g_mode_table[i].update_handler(service);
        }
    }

    return false;
}

bool mode_service_set_mode(mode_service_t *service, light_mode_t mode)
{
    if (service == NULL || !mode_service_is_valid_mode(mode))
    {
        return false;
    }

    if (mode != LIGHT_MODE_AUTO)
    {
        mode_service_note_manual_override(service);
    }

    service->powered_on = true;
    service->mode = mode;
    if (mode != LIGHT_MODE_SCENE)
    {
        mode_service_clear_scene_adjustments(service);
    }
    if (mode != LIGHT_MODE_MANUAL) {
        service->breathing_enabled = false;
    }
    if (mode == LIGHT_MODE_AUTO)
    {
        service->manual_override = false;
        service->mode = LIGHT_MODE_SCENE;
        service->scene = LIGHT_SCENE_WORK;
        return mode_service_apply_scene(service);
    }
    if (mode == LIGHT_MODE_SCENE)
    {
        return mode_service_apply_scene(service);
    }
    if (mode == LIGHT_MODE_MANUAL)
    {
        return mode_service_apply_manual(service);
    }
    if (mode == LIGHT_MODE_FLOW)
    {
        return true;
    }

    return false;
}

light_mode_t mode_service_get_mode(const mode_service_t *service)
{
    return (service != NULL) ? service->mode : LIGHT_MODE_SCENE;
}

bool mode_service_set_scene(mode_service_t *service, light_scene_t scene)
{
    if (service == NULL || !mode_service_is_valid_scene(scene))
    {
        return false;
    }

    service->scene = scene;
    mode_service_clear_scene_adjustments(service);
    mode_service_note_manual_override(service);
    service->breathing_enabled = false;
    service->powered_on = true;
    service->mode = LIGHT_MODE_SCENE;
    return mode_service_apply_scene(service);
}

bool mode_service_set_scene_with_adjustments(mode_service_t *service,
                                             light_scene_t scene,
                                             bool has_brightness_override,
                                             int brightness_pct,
                                             bool has_tone_bias_override,
                                             int tone_bias,
                                             bool has_color_temp_override,
                                             int color_temp_k)
{
    const mode_scene_config_t *config;
    int resolved_brightness_pct;
    int resolved_tone_bias;
    int resolved_color_temp_k;

    if (service == NULL || !mode_service_is_valid_scene(scene))
    {
        return false;
    }

    config = &g_scene_table[scene];
    service->scene = scene;

    resolved_brightness_pct = has_brightness_override
        ? mode_service_clamp_int(brightness_pct, BRIGHTNESS_PCT_MIN, BRIGHTNESS_PCT_MAX)
        : (int)config->brightness_pct;
    resolved_tone_bias = has_tone_bias_override
        ? mode_service_clamp_int(tone_bias, LIGHT_NODE_TONE_BIAS_MIN, LIGHT_NODE_TONE_BIAS_MAX)
        : 0;
    resolved_color_temp_k = has_color_temp_override ? color_temp_k : -1;
    if (has_color_temp_override && !color_utils_is_valid_kelvin(resolved_color_temp_k))
    {
        return false;
    }

    service->scene_brightness_pct = resolved_brightness_pct;
    service->tone_bias = has_color_temp_override ? 0 : resolved_tone_bias;
    service->scene_color_temp_k = resolved_color_temp_k;
    service->scene_modified = (service->scene_brightness_pct != (int)config->brightness_pct) ||
                              (service->tone_bias != 0) ||
                              (service->scene_color_temp_k >= 0 && service->scene_color_temp_k != config->color_temp_k);
    mode_service_note_manual_override(service);
    service->breathing_enabled = false;
    service->powered_on = true;
    service->mode = LIGHT_MODE_SCENE;
    return mode_service_apply_scene(service);
}

bool mode_service_restore_scene(mode_service_t *service)
{
    if (service == NULL)
    {
        return false;
    }
    if (service->mode != LIGHT_MODE_SCENE)
    {
        return false;
    }

    service->scene_brightness_pct = -1;
    service->tone_bias = 0;
    service->scene_color_temp_k = -1;
    service->scene_modified = false;
    service->breathing_enabled = false;
    service->powered_on = true;
    return mode_service_apply_scene(service);
}

light_scene_t mode_service_get_scene(const mode_service_t *service)
{
    return (service != NULL) ? service->scene : LIGHT_SCENE_WORK;
}

bool mode_service_get_scene_state(const mode_service_t *service,
                                  int *brightness_pct,
                                  int *tone_bias,
                                  int *color_temp_k,
                                  bool *scene_modified)
{
    const mode_scene_config_t *config;
    if (service == NULL)
    {
        return false;
    }

    config = &g_scene_table[service->scene];
    if (brightness_pct != NULL)
    {
        *brightness_pct = (service->scene_brightness_pct >= 0) ? service->scene_brightness_pct : (int)config->brightness_pct;
    }
    if (tone_bias != NULL)
    {
        *tone_bias = service->tone_bias;
    }
    if (color_temp_k != NULL)
    {
        *color_temp_k = (service->scene_color_temp_k >= 0) ? service->scene_color_temp_k : config->color_temp_k;
    }
    if (scene_modified != NULL)
    {
        *scene_modified = service->scene_modified;
    }
    return true;
}

bool mode_service_set_manual_color(mode_service_t *service,
                                   uint8_t r,
                                   uint8_t g,
                                   uint8_t b,
                                   uint8_t brightness_pct)
{
    if (service == NULL)
    {
        return false;
    }

    mode_service_note_manual_override(service);
    mode_service_clear_scene_adjustments(service);
    service->manual_r = r;
    service->manual_g = g;
    service->manual_b = b;
    service->manual_brightness_pct = (uint8_t)mode_service_clamp_int((int)brightness_pct, BRIGHTNESS_PCT_MIN, BRIGHTNESS_PCT_MAX);
    service->manual_color_temp_k = COLOR_TEMP_K_NONE;
    service->mode = LIGHT_MODE_MANUAL;
    service->powered_on = true;

    return mode_service_apply_manual(service);
}

bool mode_service_set_manual_color_temp(mode_service_t *service,
                                        uint8_t brightness_pct,
                                        int color_temp_k)
{
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;

    if (service == NULL || !color_utils_is_valid_kelvin(color_temp_k))
    {
        return false;
    }
    if (!color_utils_kelvin_to_rgb(color_temp_k, &r, &g, &b))
    {
        return false;
    }

    mode_service_note_manual_override(service);
    mode_service_clear_scene_adjustments(service);
    service->manual_r = r;
    service->manual_g = g;
    service->manual_b = b;
    service->manual_brightness_pct = (uint8_t)mode_service_clamp_int((int)brightness_pct, BRIGHTNESS_PCT_MIN, BRIGHTNESS_PCT_MAX);
    service->manual_color_temp_k = color_temp_k;
    service->mode = LIGHT_MODE_MANUAL;
    service->powered_on = true;

    return mode_service_apply_manual(service);
}

bool mode_service_set_custom_fixed(mode_service_t *service,
                                   uint8_t brightness_pct,
                                   int color_temp_k,
                                   uint8_t r,
                                   uint8_t g,
                                   uint8_t b,
                                   bool use_kelvin)
{
    if (service == NULL)
    {
        return false;
    }
    if (use_kelvin)
    {
        if (!color_utils_is_valid_kelvin(color_temp_k) ||
            !color_utils_kelvin_to_rgb(color_temp_k, &r, &g, &b))
        {
            return false;
        }
        service->manual_color_temp_k = color_temp_k;
    }
    else
    {
        service->manual_color_temp_k = COLOR_TEMP_K_NONE;
    }

    mode_service_note_manual_override(service);
    mode_service_clear_scene_adjustments(service);
    if (service->auto_service != NULL)
    {
        auto_light_service_disable(service->auto_service, "fixed");
    }
    service->manual_r = r;
    service->manual_g = g;
    service->manual_b = b;
    service->manual_brightness_pct = (uint8_t)mode_service_clamp_int((int)brightness_pct, BRIGHTNESS_PCT_MIN, BRIGHTNESS_PCT_MAX);
    service->custom_control_type = CUSTOM_CONTROL_FIXED_BRIGHTNESS;
    service->mode = LIGHT_MODE_MANUAL;
    service->powered_on = true;
    service->breathing_enabled = false;
    return mode_service_apply_manual(service);
}

bool mode_service_set_custom_target_range(mode_service_t *service,
                                          int target_lux,
                                          int tolerance_lux,
                                          int min_output_pct,
                                          int max_output_pct,
                                          int color_temp_k,
                                          uint8_t r,
                                          uint8_t g,
                                          uint8_t b,
                                          bool use_kelvin)
{
    if (service == NULL || service->auto_service == NULL)
    {
        return false;
    }
    mode_service_note_manual_override(service);
    mode_service_clear_scene_adjustments(service);
    if (!auto_light_service_configure_range(service->auto_service,
                                            target_lux,
                                            tolerance_lux,
                                            min_output_pct,
                                            max_output_pct,
                                            use_kelvin ? color_temp_k : COLOR_TEMP_K_NONE,
                                            r,
                                            g,
                                            b,
                                            5))
    {
        return false;
    }
    service->custom_control_type = CUSTOM_CONTROL_TARGET_LUX_RANGE;
    service->custom_target_lux = target_lux;
    service->custom_tolerance_lux = tolerance_lux;
    service->custom_min_output_pct = min_output_pct;
    service->custom_max_output_pct = max_output_pct;
    service->manual_r = r;
    service->manual_g = g;
    service->manual_b = b;
    service->manual_color_temp_k = use_kelvin ? color_temp_k : COLOR_TEMP_K_NONE;
    service->mode = LIGHT_MODE_MANUAL;
    service->powered_on = true;
    service->breathing_enabled = false;
    return auto_light_service_update(service->auto_service);
}

bool mode_service_get_manual_state(const mode_service_t *service,
                                   uint8_t *r,
                                   uint8_t *g,
                                   uint8_t *b,
                                   uint8_t *brightness_pct,
                                   int *color_temp_k)
{
    if (service == NULL)
    {
        return false;
    }

    if (r != NULL) *r = service->manual_r;
    if (g != NULL) *g = service->manual_g;
    if (b != NULL) *b = service->manual_b;
    if (brightness_pct != NULL) *brightness_pct = service->manual_brightness_pct;
    if (color_temp_k != NULL) *color_temp_k = service->manual_color_temp_k;
    return true;
}

bool mode_service_get_control_status(const mode_service_t *service,
                                     float *current_lux,
                                     int *target_lux,
                                     int *tolerance_lux,
                                     int *target_lux_min,
                                     int *target_lux_max,
                                     int *led_output_percent,
                                     const char **control_state,
                                     bool *sensor_ok,
                                     const char **custom_control_type)
{
    const char *state = "off";
    bool ok = false;
    if (service == NULL)
    {
        return false;
    }
    if (service->auto_service != NULL)
    {
        ok = auto_light_service_get_control_status(service->auto_service,
                                                   current_lux,
                                                   target_lux,
                                                   tolerance_lux,
                                                   target_lux_min,
                                                   target_lux_max,
                                                   led_output_percent,
                                                   &state,
                                                   sensor_ok);
    }
    if (!ok)
    {
        uint8_t brightness = 0u;
        if (current_lux != NULL) *current_lux = -1.0f;
        if (target_lux != NULL) *target_lux = -1;
        if (tolerance_lux != NULL) *tolerance_lux = -1;
        if (target_lux_min != NULL) *target_lux_min = -1;
        if (target_lux_max != NULL) *target_lux_max = -1;
        if (service->led_service != NULL && led_output_percent != NULL)
        {
            led_service_get_state(service->led_service, NULL, NULL, NULL, &brightness);
            *led_output_percent = mode_service_u8_to_pct(brightness);
        }
        if (sensor_ok != NULL) *sensor_ok = false;
    }
    if (!service->powered_on || (service->mode == LIGHT_MODE_SCENE && service->scene == LIGHT_SCENE_VACANT))
    {
        state = "off";
    }
    else if (service->mode == LIGHT_MODE_MANUAL && service->custom_control_type == CUSTOM_CONTROL_FIXED_BRIGHTNESS)
    {
        state = "fixed";
    }
    if (control_state != NULL) *control_state = state;
    if (custom_control_type != NULL) *custom_control_type = mode_service_custom_control_type_to_string(service->custom_control_type);
    return true;
}

bool mode_service_set_flow_preset(mode_service_t *service, flow_preset_t preset)
{
    if (service == NULL || service->flow_service == NULL)
    {
        return false;
    }

    mode_service_note_manual_override(service);
    mode_service_clear_scene_adjustments(service);
    service->breathing_enabled = false;
    service->mode = LIGHT_MODE_FLOW;
    service->powered_on = true;
    return flow_service_set_preset(service->flow_service, preset);
}

flow_preset_t mode_service_get_flow_preset(const mode_service_t *service)
{
    if (service == NULL || service->flow_service == NULL)
    {
        return FLOW_PRESET_WARM_AMBIENT;
    }

    return flow_service_get_preset(service->flow_service);
}

bool mode_service_set_flow_speed(mode_service_t *service, uint8_t speed)
{
    if (service == NULL || service->flow_service == NULL)
    {
        return false;
    }

    mode_service_note_manual_override(service);
    mode_service_clear_scene_adjustments(service);
    service->breathing_enabled = false;
    service->mode = LIGHT_MODE_FLOW;
    service->powered_on = true;
    return flow_service_set_speed(service->flow_service, speed);
}

bool mode_service_set_flow_brightness(mode_service_t *service, uint8_t brightness_pct)
{
    if (service == NULL || service->flow_service == NULL)
    {
        return false;
    }

    mode_service_note_manual_override(service);
    mode_service_clear_scene_adjustments(service);
    service->breathing_enabled = false;
    service->mode = LIGHT_MODE_FLOW;
    service->powered_on = true;
    return flow_service_set_brightness(service->flow_service, mode_service_pct_to_u8(brightness_pct));
}

bool mode_service_set_flow_soft_mode(mode_service_t *service, bool enabled)
{
    if (service == NULL || service->flow_service == NULL)
    {
        return false;
    }

    mode_service_clear_scene_adjustments(service);
    service->breathing_enabled = false;
    service->mode = LIGHT_MODE_FLOW;
    service->powered_on = true;
    return flow_service_set_soft_mode(service->flow_service, enabled);
}

bool mode_service_set_flow_gradient(mode_service_t *service,
                                    uint8_t r1, uint8_t g1, uint8_t b1,
                                    uint8_t r2, uint8_t g2, uint8_t b2,
                                    uint8_t r3, uint8_t g3, uint8_t b3)
{
    if (service == NULL || service->flow_service == NULL)
    {
        return false;
    }

    mode_service_note_manual_override(service);
    mode_service_clear_scene_adjustments(service);
    service->breathing_enabled = false;
    service->mode = LIGHT_MODE_FLOW;
    service->powered_on = true;
    return flow_service_set_gradient(service->flow_service, r1, g1, b1, r2, g2, b2, r3, g3, b3);
}

bool mode_service_set_auto_mode(mode_service_t *service, bool enabled)
{
    if (service == NULL)
    {
        return false;
    }

    service->powered_on = true;
    if (enabled)
    {
        mode_service_clear_scene_adjustments(service);
        service->scene = LIGHT_SCENE_WORK;
        service->mode = LIGHT_MODE_SCENE;
        service->breathing_enabled = false;
        service->manual_override = false;
        return mode_service_apply_scene(service);
    }

    if (service->mode == LIGHT_MODE_AUTO || service->mode == LIGHT_MODE_SCENE)
    {
        service->scene = LIGHT_SCENE_VACANT;
        mode_service_clear_scene_adjustments(service);
        service->mode = LIGHT_MODE_SCENE;
        service->manual_override = false;
        return mode_service_apply_scene(service);
    }

    return true;
}

bool mode_service_get_manual_override(const mode_service_t *service)
{
    return (service != NULL) && service->manual_override;
}

bool mode_service_set_breathing(mode_service_t *service,
                                bool enabled,
                                breathing_speed_t speed,
                                breathing_strength_t strength)
{
    if (service == NULL)
    {
        return false;
    }
    if (service->mode != LIGHT_MODE_MANUAL)
    {
        return false;
    }

    service->breathing_enabled = enabled;
    service->breathing_speed = speed;
    service->breathing_strength = strength;
    service->breathing_phase = 50u;
    service->breathing_forward = true;
    service->breathing_last_step_ms = 0u;
    return true;
}

bool mode_service_get_breathing(mode_service_t *service,
                                bool *enabled,
                                breathing_speed_t *speed,
                                breathing_strength_t *strength)
{
    if (service == NULL)
    {
        return false;
    }

    if (enabled != NULL) *enabled = service->breathing_enabled;
    if (speed != NULL) *speed = service->breathing_speed;
    if (strength != NULL) *strength = service->breathing_strength;
    return true;
}

bool mode_service_power_off(mode_service_t *service)
{
    if (service == NULL || service->led_service == NULL)
    {
        return false;
    }

    service->powered_on = false;
    mode_service_clear_scene_adjustments(service);
    service->breathing_enabled = false;
    led_service_off(service->led_service);
    return true;
}
