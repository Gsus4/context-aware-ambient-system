#include "led_service.h"

#include <stddef.h>

#include "pico/time.h"

#include "../config.h"

static uint8_t step_toward_u8(uint8_t current, uint8_t target, uint8_t step)
{
    if (current < target)
    {
        int next_value = (int)current + (int)step;
        if (next_value > target)
        {
            return target;
        }
        return (uint8_t)next_value;
    }

    if (current > target)
    {
        int next_value = (int)current - (int)step;
        if (next_value < target)
        {
            return target;
        }
        return (uint8_t)next_value;
    }

    return current;
}

bool led_service_init(led_service_t *service,
                      ws2812_t *device,
                      uint8_t brightness)
{
    if (service == NULL || device == NULL)
    {
        return false;
    }

    service->device = device;

    service->brightness = brightness;
    service->red = 0;
    service->green = 0;
    service->blue = 0;

    service->target_brightness = brightness;
    service->target_red = 0;
    service->target_green = 0;
    service->target_blue = 0;
    service->target_dirty = true;
    service->last_transition_ms = 0u;

    return true;
}

bool led_service_set_brightness(led_service_t *service, uint8_t brightness)
{
    if (service == NULL)
    {
        return false;
    }

    service->brightness = brightness;
    return true;
}

uint8_t led_service_get_brightness(const led_service_t *service)
{
    if (service == NULL)
    {
        return 0;
    }

    return service->brightness;
}

void led_service_set_color(led_service_t *service,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue)
{
    if (service == NULL || service->device == NULL)
    {
        return;
    }

    service->red = red;
    service->green = green;
    service->blue = blue;

    uint8_t out_r = ws2812_apply_brightness(red, service->brightness);
    uint8_t out_g = ws2812_apply_brightness(green, service->brightness);
    uint8_t out_b = ws2812_apply_brightness(blue, service->brightness);

    ws2812_fill(service->device, out_r, out_g, out_b);
}

bool led_service_set_pixel(led_service_t *service,
                           uint16_t index,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue)
{
    if (service == NULL || service->device == NULL)
    {
        return false;
    }

    uint8_t out_r = ws2812_apply_brightness(red, service->brightness);
    uint8_t out_g = ws2812_apply_brightness(green, service->brightness);
    uint8_t out_b = ws2812_apply_brightness(blue, service->brightness);

    ws2812_set_pixel(service->device, index, out_r, out_g, out_b);
    return true;
}

void led_service_clear(led_service_t *service)
{
    if (service == NULL || service->device == NULL)
    {
        return;
    }

    service->red = 0;
    service->green = 0;
    service->blue = 0;
    ws2812_clear(service->device);
}

bool led_service_set_target_brightness(led_service_t *service,
                                       uint8_t brightness)
{
    if (service == NULL)
    {
        return false;
    }

    if (service->target_brightness != brightness)
    {
        service->target_brightness = brightness;
        service->target_dirty = true;
    }
    return true;
}

bool led_service_set_target_color(led_service_t *service,
                                  uint8_t red,
                                  uint8_t green,
                                  uint8_t blue)
{
    if (service == NULL)
    {
        return false;
    }

    if (service->target_red != red ||
        service->target_green != green ||
        service->target_blue != blue)
    {
        service->target_red = red;
        service->target_green = green;
        service->target_blue = blue;
        service->target_dirty = true;
    }
    return true;
}

bool led_service_sync_to_target(led_service_t *service)
{
    if (service == NULL || service->device == NULL)
    {
        return false;
    }

    if (!service->target_dirty && led_service_is_at_target(service))
    {
        return true;
    }

    service->brightness = service->target_brightness;
    service->red = service->target_red;
    service->green = service->target_green;
    service->blue = service->target_blue;

    led_service_set_color(service,
                          service->red,
                          service->green,
                          service->blue);
    led_service_show(service);
    service->target_dirty = false;
    service->last_transition_ms = to_ms_since_boot(get_absolute_time());
    return true;
}

bool led_service_step_transition(led_service_t *service)
{
    uint32_t now_ms;

    if (service == NULL || service->device == NULL)
    {
        return false;
    }

    if (!service->target_dirty && led_service_is_at_target(service))
    {
        return true;
    }

    now_ms = to_ms_since_boot(get_absolute_time());
    if (!service->target_dirty &&
        service->last_transition_ms != 0u &&
        (now_ms - service->last_transition_ms) < LED_TRANSITION_UPDATE_INTERVAL_MS)
    {
        return true;
    }

    service->brightness = step_toward_u8(service->brightness,
                                         service->target_brightness,
                                         LED_TRANSITION_BRIGHTNESS_STEP);

    service->red = step_toward_u8(service->red,
                                  service->target_red,
                                  LED_TRANSITION_COLOR_STEP);

    service->green = step_toward_u8(service->green,
                                    service->target_green,
                                    LED_TRANSITION_COLOR_STEP);

    service->blue = step_toward_u8(service->blue,
                                   service->target_blue,
                                   LED_TRANSITION_COLOR_STEP);

    led_service_set_color(service,
                          service->red,
                          service->green,
                          service->blue);

    led_service_show(service);
    service->target_dirty = false;
    service->last_transition_ms = now_ms;
    return true;
}

bool led_service_is_at_target(const led_service_t *service)
{
    if (service == NULL)
    {
        return false;
    }

    return (service->brightness == service->target_brightness) &&
           (service->red == service->target_red) &&
           (service->green == service->target_green) &&
           (service->blue == service->target_blue);
}

void led_service_off(led_service_t *service)
{
    if (service == NULL)
    {
        return;
    }

    if (service->brightness == 0u &&
        service->red == 0u &&
        service->green == 0u &&
        service->blue == 0u &&
        service->target_brightness == 0u &&
        service->target_red == 0u &&
        service->target_green == 0u &&
        service->target_blue == 0u &&
        !service->target_dirty)
    {
        return;
    }

    service->brightness = 0;
    service->red = 0;
    service->green = 0;
    service->blue = 0;
    service->target_brightness = 0;
    service->target_red = 0;
    service->target_green = 0;
    service->target_blue = 0;
    service->target_dirty = false;

    if (service->device != NULL)
    {
        ws2812_clear(service->device);
        ws2812_show(service->device);
        service->last_transition_ms = to_ms_since_boot(get_absolute_time());
    }
}

void led_service_show(led_service_t *service)
{
    if (service == NULL || service->device == NULL)
    {
        return;
    }

    ws2812_show(service->device);
}

void led_service_set_state(led_service_t *service,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue,
                           uint8_t brightness)
{
    if (service == NULL)
    {
        return;
    }

    service->red = red;
    service->green = green;
    service->blue = blue;
    service->brightness = brightness;
}

void led_service_get_state(const led_service_t *service,
                           uint8_t *red,
                           uint8_t *green,
                           uint8_t *blue,
                           uint8_t *brightness)
{
    if (service == NULL)
    {
        return;
    }

    if (red != NULL)
    {
        *red = service->red;
    }

    if (green != NULL)
    {
        *green = service->green;
    }

    if (blue != NULL)
    {
        *blue = service->blue;
    }

    if (brightness != NULL)
    {
        *brightness = service->brightness;
    }
}