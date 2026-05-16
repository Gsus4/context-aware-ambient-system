#include <stdlib.h>
#include <string.h>

#include "hardware/clocks.h"

#include "ws2812.h"
#include "ws2812.pio.h"

static inline void ws2812_put_pixel(PIO pio, uint sm, uint32_t pixel_grb)
{
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint32_t)green << 16) | ((uint32_t)red << 8) | (uint32_t)blue;
}

bool ws2812_init_device(ws2812_t *device, PIO pio, uint sm, uint data_pin, uint led_count)
{
    uint offset;
    pio_sm_config config;
    int cycles_per_bit;
    float div;

    if (device == NULL || led_count == 0u)
    {
        return false;
    }

    memset(device, 0, sizeof(*device));
    device->pixel_buffer = (uint32_t *)calloc(led_count, sizeof(uint32_t));
    if (device->pixel_buffer == NULL)
    {
        return false;
    }

    offset = pio_add_program(pio, &ws2812_program);
    config = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&config, data_pin);
    sm_config_set_out_shift(&config, false, true, 24);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);

    pio_gpio_init(pio, data_pin);
    pio_sm_set_consecutive_pindirs(pio, sm, data_pin, 1, true);

    cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
    div = (float)clock_get_hz(clk_sys) / (800000.0f * cycles_per_bit);
    sm_config_set_clkdiv(&config, div);

    pio_sm_init(pio, sm, offset, &config);
    pio_sm_set_enabled(pio, sm, true);

    device->pio = pio;
    device->sm = sm;
    device->offset = offset;
    device->data_pin = data_pin;
    device->led_count = led_count;
    return true;
}

void ws2812_deinit_device(ws2812_t *device)
{
    if (device == NULL)
    {
        return;
    }

    free(device->pixel_buffer);
    device->pixel_buffer = NULL;
    device->led_count = 0u;
}

void ws2812_set_pixel(ws2812_t *device, uint index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (device == NULL || device->pixel_buffer == NULL || index >= device->led_count)
    {
        return;
    }
    device->pixel_buffer[index] = urgb_u32(red, green, blue);
}

void ws2812_fill(ws2812_t *device, uint8_t red, uint8_t green, uint8_t blue)
{
    uint i;
    if (device == NULL || device->pixel_buffer == NULL)
    {
        return;
    }
    for (i = 0u; i < device->led_count; ++i)
    {
        device->pixel_buffer[i] = urgb_u32(red, green, blue);
    }
}

void ws2812_clear(ws2812_t *device)
{
    if (device == NULL || device->pixel_buffer == NULL)
    {
        return;
    }
    memset(device->pixel_buffer, 0, sizeof(uint32_t) * device->led_count);
}

void ws2812_show(ws2812_t *device)
{
    uint i;
    if (device == NULL || device->pixel_buffer == NULL)
    {
        return;
    }
    for (i = 0u; i < device->led_count; ++i)
    {
        ws2812_put_pixel(device->pio, device->sm, device->pixel_buffer[i]);
    }
    sleep_us(80);
}

uint8_t ws2812_apply_brightness(uint8_t color, uint8_t brightness)
{
    return (uint8_t)(((uint16_t)color * (uint16_t)brightness) / 255u);
}
