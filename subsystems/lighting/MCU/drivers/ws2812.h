#ifndef WS2812_H
#define WS2812_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"
#include "pico/stdlib.h"

typedef struct
{
    PIO pio;
    uint sm;
    uint offset;
    uint data_pin;
    uint led_count;
    uint32_t *pixel_buffer;
} ws2812_t;

bool ws2812_init_device(ws2812_t *device, PIO pio, uint sm, uint data_pin, uint led_count);
void ws2812_deinit_device(ws2812_t *device);
void ws2812_set_pixel(ws2812_t *device, uint index, uint8_t red, uint8_t green, uint8_t blue);
void ws2812_fill(ws2812_t *device, uint8_t red, uint8_t green, uint8_t blue);
void ws2812_clear(ws2812_t *device);
void ws2812_show(ws2812_t *device);
uint8_t ws2812_apply_brightness(uint8_t color, uint8_t brightness);

#endif
