#ifndef MODE_SERVICE_H
#define MODE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "auto_light_service.h"
#include "flow_service.h"
#include "led_service.h"

typedef enum
{
    LIGHT_MODE_AUTO = 0,
    LIGHT_MODE_SCENE,
    LIGHT_MODE_MANUAL,
    LIGHT_MODE_FLOW
} light_mode_t;

typedef enum
{
    LIGHT_SCENE_VACANT = 0,
    LIGHT_SCENE_SLEEP,
    LIGHT_SCENE_RELAX,
    LIGHT_SCENE_WORK,
    LIGHT_SCENE_EXERCISE
} light_scene_t;

typedef enum
{
    CUSTOM_CONTROL_FIXED_BRIGHTNESS = 0,
    CUSTOM_CONTROL_TARGET_LUX_RANGE
} custom_control_type_t;

typedef enum
{
    MODE_COLOR_OFF = 0,
    MODE_COLOR_CCT,
    MODE_COLOR_RGB
} mode_color_type_t;

typedef enum
{
    BREATHING_SPEED_SLOW = 0,
    BREATHING_SPEED_MEDIUM,
    BREATHING_SPEED_FAST
} breathing_speed_t;

typedef enum
{
    BREATHING_STRENGTH_LOW = 0,
    BREATHING_STRENGTH_MEDIUM,
    BREATHING_STRENGTH_HIGH
} breathing_strength_t;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t brightness_pct;
    int color_temp_k;
    int target_lux;
    int tolerance_lux;
    int min_output_pct;
    int max_output_pct;
    int output_step_pct;
    mode_color_type_t color_type;
    bool adaptive_enabled;
} mode_scene_config_t;

typedef struct
{
    light_mode_t mode;
    light_scene_t scene;
    bool scene_modified;
    int scene_brightness_pct;
    int tone_bias;
    int scene_color_temp_k;
    int manual_color_temp_k;
    bool manual_override;
    bool breathing_enabled;
    breathing_speed_t breathing_speed;
    breathing_strength_t breathing_strength;
    bool powered_on;

    auto_light_service_t *auto_service;
    led_service_t *led_service;
    flow_service_t *flow_service;

    uint8_t manual_r;
    uint8_t manual_g;
    uint8_t manual_b;
    uint8_t manual_brightness_pct;
    custom_control_type_t custom_control_type;
    int custom_target_lux;
    int custom_tolerance_lux;
    int custom_min_output_pct;
    int custom_max_output_pct;

    uint8_t breathing_phase;
    bool breathing_forward;
    uint32_t breathing_last_step_ms;
} mode_service_t;

bool mode_service_init(mode_service_t *service,
                       auto_light_service_t *auto_service,
                       led_service_t *led_service,
                       flow_service_t *flow_service);

bool mode_service_update(mode_service_t *service);

bool mode_service_set_mode(mode_service_t *service, light_mode_t mode);
light_mode_t mode_service_get_mode(const mode_service_t *service);

bool mode_service_set_scene(mode_service_t *service, light_scene_t scene);
bool mode_service_set_scene_with_adjustments(mode_service_t *service,
                                             light_scene_t scene,
                                             bool has_brightness_override,
                                             int brightness_pct,
                                             bool has_tone_bias_override,
                                             int tone_bias,
                                             bool has_color_temp_override,
                                             int color_temp_k);
bool mode_service_restore_scene(mode_service_t *service);
light_scene_t mode_service_get_scene(const mode_service_t *service);
bool mode_service_get_scene_state(const mode_service_t *service,
                                  int *brightness_pct,
                                  int *tone_bias,
                                  int *color_temp_k,
                                  bool *scene_modified);

bool mode_service_set_manual_color(mode_service_t *service,
                                   uint8_t r,
                                   uint8_t g,
                                   uint8_t b,
                                   uint8_t brightness_pct);

bool mode_service_set_manual_color_temp(mode_service_t *service,
                                        uint8_t brightness_pct,
                                        int color_temp_k);
bool mode_service_set_custom_fixed(mode_service_t *service,
                                   uint8_t brightness_pct,
                                   int color_temp_k,
                                   uint8_t r,
                                   uint8_t g,
                                   uint8_t b,
                                   bool use_kelvin);
bool mode_service_set_custom_target_range(mode_service_t *service,
                                          int target_lux,
                                          int tolerance_lux,
                                          int min_output_pct,
                                          int max_output_pct,
                                          int color_temp_k,
                                          uint8_t r,
                                          uint8_t g,
                                          uint8_t b,
                                          bool use_kelvin);
const char *mode_service_custom_control_type_to_string(custom_control_type_t type);

bool mode_service_get_manual_state(const mode_service_t *service,
                                   uint8_t *r,
                                   uint8_t *g,
                                   uint8_t *b,
                                   uint8_t *brightness_pct,
                                   int *color_temp_k);
bool mode_service_get_control_status(const mode_service_t *service,
                                     float *current_lux,
                                     int *target_lux,
                                     int *tolerance_lux,
                                     int *target_lux_min,
                                     int *target_lux_max,
                                     int *led_output_percent,
                                     const char **control_state,
                                     bool *sensor_ok,
                                     const char **custom_control_type);

bool mode_service_apply_manual(mode_service_t *service);
bool mode_service_apply_scene(mode_service_t *service);

bool mode_service_set_flow_preset(mode_service_t *service, flow_preset_t preset);
flow_preset_t mode_service_get_flow_preset(const mode_service_t *service);

bool mode_service_set_flow_speed(mode_service_t *service, uint8_t speed);
bool mode_service_set_flow_brightness(mode_service_t *service, uint8_t brightness_pct);
bool mode_service_set_flow_soft_mode(mode_service_t *service, bool enabled);
bool mode_service_set_flow_gradient(mode_service_t *service,
                                    uint8_t r1, uint8_t g1, uint8_t b1,
                                    uint8_t r2, uint8_t g2, uint8_t b2,
                                    uint8_t r3, uint8_t g3, uint8_t b3);

bool mode_service_set_auto_mode(mode_service_t *service, bool enabled);
bool mode_service_get_manual_override(const mode_service_t *service);

bool mode_service_set_breathing(mode_service_t *service,
                                bool enabled,
                                breathing_speed_t speed,
                                breathing_strength_t strength);
bool mode_service_get_breathing(mode_service_t *service,
                                bool *enabled,
                                breathing_speed_t *speed,
                                breathing_strength_t *strength);

bool mode_service_power_off(mode_service_t *service);

const char *mode_service_mode_to_string(light_mode_t mode);
const char *mode_service_scene_to_string(light_scene_t scene);
const char *mode_service_breathing_speed_to_string(breathing_speed_t speed);
const char *mode_service_breathing_strength_to_string(breathing_strength_t strength);
const char *light_mode_to_string(light_mode_t mode);
const char *scene_mode_to_string(light_scene_t scene);

#endif
