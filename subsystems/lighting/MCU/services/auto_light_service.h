#ifndef AUTO_LIGHT_SERVICE_H
#define AUTO_LIGHT_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "light_sensor_service.h"
#include "led_service.h"

typedef struct
{
    float filtered_lux;
    float last_output_lux;
    bool lux_filter_initialized;
    light_sensor_service_t *light_service;
    led_service_t *led_service;
    uint32_t last_valid_read_ms;
    uint32_t last_sensor_read_ms;
    uint32_t last_switch_ms;
    int consecutive_failures;
    int current_zone;
    int current_brightness_pct;
    int current_color_temp_k;
    int target_lux;
    int tolerance_lux;
    int target_lux_min;
    int target_lux_max;
    int min_output_pct;
    int max_output_pct;
    int output_step_pct;
    uint8_t target_r;
    uint8_t target_g;
    uint8_t target_b;
    char control_state[32];
    bool adaptive_enabled;
    bool sensor_ok;
    bool pending_sensor_error;
    bool pending_sensor_recovered;
    bool pending_fallback_notice;
    bool fault_active;
    bool refresh_requested;
    char pending_error_reason[32];
    char last_error_reason[32];
} auto_light_service_t;

bool auto_light_service_init(auto_light_service_t *service,
                             light_sensor_service_t *light_service,
                             led_service_t *led_service);
bool auto_light_service_update(auto_light_service_t *service);
bool auto_light_service_get_filtered_lux(const auto_light_service_t *service,
                                         float *lux_value);
bool auto_light_service_get_last_output_lux(const auto_light_service_t *service,
                                            float *lux_value);
bool auto_light_service_get_current_output(const auto_light_service_t *service,
                                           int *brightness_pct,
                                           int *color_temp_k);
bool auto_light_service_configure_range(auto_light_service_t *service,
                                        int target_lux,
                                        int tolerance_lux,
                                        int min_output_pct,
                                        int max_output_pct,
                                        int color_temp_k,
                                        uint8_t r,
                                        uint8_t g,
                                        uint8_t b,
                                        int output_step_pct);
bool auto_light_service_disable(auto_light_service_t *service, const char *state);
bool auto_light_service_get_control_status(const auto_light_service_t *service,
                                           float *current_lux,
                                           int *target_lux,
                                           int *tolerance_lux,
                                           int *target_lux_min,
                                           int *target_lux_max,
                                           int *led_output_percent,
                                           const char **control_state,
                                           bool *sensor_ok);
bool auto_light_service_request_refresh(auto_light_service_t *service);
bool auto_light_service_consume_error(auto_light_service_t *service,
                                      char *reason,
                                      uint16_t reason_size);
bool auto_light_service_consume_recovery(auto_light_service_t *service);
bool auto_light_service_consume_fallback_notice(auto_light_service_t *service,
                                                char *reason,
                                                uint16_t reason_size);

#endif
