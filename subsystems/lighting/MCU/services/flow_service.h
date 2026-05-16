#ifndef FLOW_SERVICE_H
#define FLOW_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "led_service.h"

typedef enum
{
    FLOW_PRESET_WARM_AMBIENT = 0,
    FLOW_PRESET_SUNSET,
    FLOW_PRESET_OCEAN,
    FLOW_PRESET_AURORA,
    FLOW_PRESET_LAVENDER,
    FLOW_PRESET_SOFT_RAINBOW
} flow_preset_t;

#define FLOW_MAX_PALETTE_COLORS 6u

typedef struct
{
    led_service_t *led_service;

    bool enabled;
    flow_preset_t preset;
    uint8_t speed;
    uint8_t brightness;
    bool soft_mode;
    uint16_t step;
    uint32_t last_update_ms;

    uint8_t palette_count;
    uint8_t repeat_count;
    uint8_t color_r[FLOW_MAX_PALETTE_COLORS];
    uint8_t color_g[FLOW_MAX_PALETTE_COLORS];
    uint8_t color_b[FLOW_MAX_PALETTE_COLORS];

    uint8_t soft_breath_phase;
    bool soft_breath_forward;
    uint32_t soft_breath_last_ms;
} flow_service_t;

const char *flow_service_preset_to_string(flow_preset_t preset);
const char *flow_preset_to_string(flow_preset_t preset);
const char *flow_service_speed_to_string(uint8_t speed);

bool flow_service_init(flow_service_t *service, led_service_t *led_service);
bool flow_service_update(flow_service_t *service);

bool flow_service_is_enabled(const flow_service_t *service);

bool flow_service_set_preset(flow_service_t *service, flow_preset_t preset);
flow_preset_t flow_service_get_preset(const flow_service_t *service);

bool flow_service_set_speed(flow_service_t *service, uint8_t speed);
uint8_t flow_service_get_speed(const flow_service_t *service);

bool flow_service_set_brightness(flow_service_t *service, uint8_t brightness);
uint8_t flow_service_get_brightness(const flow_service_t *service);

bool flow_service_set_soft_mode(flow_service_t *service, bool enabled);
bool flow_service_get_soft_mode(const flow_service_t *service);
bool flow_service_soft_mode_supported(flow_preset_t preset);

bool flow_service_set_gradient(flow_service_t *service,
                               uint8_t r1, uint8_t g1, uint8_t b1,
                               uint8_t r2, uint8_t g2, uint8_t b2,
                               uint8_t r3, uint8_t g3, uint8_t b3);

void flow_service_get_gradient(const flow_service_t *service,
                               uint8_t *r1, uint8_t *g1, uint8_t *b1,
                               uint8_t *r2, uint8_t *g2, uint8_t *b2,
                               uint8_t *r3, uint8_t *g3, uint8_t *b3);

#endif
