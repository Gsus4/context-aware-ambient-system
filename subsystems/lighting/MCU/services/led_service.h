#ifndef LED_SERVICE_H
#define LED_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "../drivers/ws2812.h"

typedef struct
{
    ws2812_t *device;

    uint8_t brightness;
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    uint8_t target_brightness;
    uint8_t target_red;
    uint8_t target_green;
    uint8_t target_blue;

    bool target_dirty;
    uint32_t last_transition_ms;
} led_service_t;

bool led_service_init(led_service_t *service,
                      ws2812_t *device,
                      uint8_t brightness);

bool led_service_set_brightness(led_service_t *service, uint8_t brightness);
uint8_t led_service_get_brightness(const led_service_t *service);

void led_service_set_color(led_service_t *service,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue);

bool led_service_set_pixel(led_service_t *service,
                           uint16_t index,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue);

void led_service_clear(led_service_t *service);

bool led_service_set_target_brightness(led_service_t *service,
                                       uint8_t brightness);

bool led_service_set_target_color(led_service_t *service,
                                  uint8_t red,
                                  uint8_t green,
                                  uint8_t blue);

bool led_service_sync_to_target(led_service_t *service);
bool led_service_step_transition(led_service_t *service);
bool led_service_is_at_target(const led_service_t *service);
void led_service_off(led_service_t *service);
void led_service_show(led_service_t *service);

void led_service_set_state(led_service_t *service,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue,
                           uint8_t brightness);

void led_service_get_state(const led_service_t *service,
                           uint8_t *red,
                           uint8_t *green,
                           uint8_t *blue,
                           uint8_t *brightness);

#endif
