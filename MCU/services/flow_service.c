#include "flow_service.h"

#include <stddef.h>

#include "pico/time.h"

#include "../config.h"

#define FLOW_PHASE_MAX 255u
#define FLOW_MIN_MOVEMENT_STEP 1u
#define FLOW_SPEED_DIVISOR 16u
#define FLOW_SPEED_SLOW_VALUE 40u
#define FLOW_SPEED_MEDIUM_VALUE 80u
#define FLOW_SPEED_FAST_VALUE 128u
#define FLOW_SOFT_MODE_BRIGHTNESS_MAX ((FLOW_SOFT_MODE_BRIGHTNESS_MAX_PCT * 255u) / 100u)
#define FLOW_SOFT_MODE_BRIGHTNESS_MAX_AURORA 127u
#define FLOW_SOFT_MODE_BREATH_INTERVAL_MS 90u
#define FLOW_SOFT_MODE_BREATH_AMPLITUDE_PCT 10u

#define FLOW_LUMA_WAVE_AMPLITUDE_PCT 12u
#define FLOW_LUMA_WAVE_AMPLITUDE_SOFT_PCT 5u
#define FLOW_LUMA_WAVE_MIN_BRIGHTNESS_PCT 8u
#define FLOW_LUMA_WAVE_SOFT_MIN_BRIGHTNESS_PCT 12u
#define FLOW_LUMA_WAVE_PHASE_DIVISOR 2u

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} flow_color_t;

static uint8_t flow_clamp_u8_from_int(int value)
{
    if (value < 0)
    {
        return 0u;
    }
    if (value > 255)
    {
        return 255u;
    }
    return (uint8_t)value;
}

static uint8_t lerp_u8(uint8_t a, uint8_t b, uint8_t t)
{
    uint16_t aa = (uint16_t)a * (uint16_t)(255u - t);
    uint16_t bb = (uint16_t)b * (uint16_t)t;
    return (uint8_t)((aa + bb + 127u) / 255u);
}

static uint8_t ease_in_out_u8(uint8_t t)
{
    uint32_t x = t;
    uint32_t y = (x * x * (765u - (2u * x))) / 65025u;
    if (y > 255u)
    {
        y = 255u;
    }
    return (uint8_t)y;
}

static void flow_service_clear_palette(flow_service_t *service)
{
    uint8_t i;

    if (service == NULL)
    {
        return;
    }

    service->palette_count = 0u;
    service->repeat_count = 1u;
    for (i = 0u; i < FLOW_MAX_PALETTE_COLORS; ++i)
    {
        service->color_r[i] = 0u;
        service->color_g[i] = 0u;
        service->color_b[i] = 0u;
    }
}

static void flow_service_set_palette_color(flow_service_t *service,
                                           uint8_t index,
                                           uint8_t r,
                                           uint8_t g,
                                           uint8_t b)
{
    if (service == NULL || index >= FLOW_MAX_PALETTE_COLORS)
    {
        return;
    }

    service->color_r[index] = r;
    service->color_g[index] = g;
    service->color_b[index] = b;

    if ((uint8_t)(index + 1u) > service->palette_count)
    {
        service->palette_count = (uint8_t)(index + 1u);
    }
}

static flow_color_t flow_service_get_palette_color(const flow_service_t *service, uint8_t index)
{
    flow_color_t color = {0u, 0u, 0u};

    if (service == NULL || service->palette_count == 0u)
    {
        return color;
    }

    index = (uint8_t)(index % service->palette_count);
    color.r = service->color_r[index];
    color.g = service->color_g[index];
    color.b = service->color_b[index];
    return color;
}

static flow_color_t gradient_palette_color(const flow_service_t *service, uint8_t phase)
{
    flow_color_t color = {0u, 0u, 0u};
    uint8_t palette_count;
    uint16_t scaled;
    uint8_t segment_index;
    uint8_t local_t;
    flow_color_t start_color;
    flow_color_t end_color;
    uint8_t eased_t;

    if (service == NULL || service->palette_count == 0u)
    {
        return color;
    }

    palette_count = service->palette_count;
    if (palette_count == 1u)
    {
        return flow_service_get_palette_color(service, 0u);
    }

    scaled = (uint16_t)phase * (uint16_t)palette_count;
    segment_index = (uint8_t)(scaled >> 8);
    local_t = (uint8_t)(scaled & 0xFFu);

    start_color = flow_service_get_palette_color(service, segment_index);
    end_color = flow_service_get_palette_color(service, (uint8_t)((segment_index + 1u) % palette_count));
    eased_t = ease_in_out_u8(local_t);

    color.r = lerp_u8(start_color.r, end_color.r, eased_t);
    color.g = lerp_u8(start_color.g, end_color.g, eased_t);
    color.b = lerp_u8(start_color.b, end_color.b, eased_t);
    return color;
}

static uint8_t compute_palette_phase(uint16_t step,
                                     uint16_t led_index,
                                     uint16_t led_count,
                                     uint8_t repeat_count)
{
    uint32_t spatial_phase;
    uint32_t combined;

    if (led_count == 0u)
    {
        return 0u;
    }

    if (repeat_count == 0u)
    {
        repeat_count = 1u;
    }

    spatial_phase = (((uint32_t)led_index * (uint32_t)repeat_count * 256u) + ((uint32_t)led_count / 2u)) / (uint32_t)led_count;
    combined = ((uint32_t)step + spatial_phase) & FLOW_PHASE_MAX;
    return (uint8_t)combined;
}

static uint8_t flow_service_effective_repeat_count(const flow_service_t *service, uint16_t led_count)
{
    uint8_t repeat_count;

    if (service == NULL)
    {
        return 1u;
    }

    repeat_count = service->repeat_count;
    if (repeat_count == 0u)
    {
        repeat_count = 1u;
    }

    /*
     * 長燈條版本：LED 顆數變多時，適度增加色帶重複次數，
     * 讓 flow 在 60 / 120 / 300 顆等較長燈條上仍然看得出明顯流動與配色分段。
     */
    if (led_count <= 12u)
    {
        return 1u;
    }
    if (led_count <= 30u)
    {
        return repeat_count;
    }
    if (led_count <= 60u)
    {
        repeat_count = (uint8_t)(repeat_count + 1u);
    }
    else if (led_count <= 120u)
    {
        repeat_count = (uint8_t)(repeat_count + 2u);
    }
    else if (led_count <= 180u)
    {
        repeat_count = (uint8_t)(repeat_count + 3u);
    }
    else if (led_count <= 240u)
    {
        repeat_count = (uint8_t)(repeat_count + 4u);
    }
    else
    {
        repeat_count = (uint8_t)(repeat_count + 5u);
    }

    if (repeat_count > 8u)
    {
        repeat_count = 8u;
    }

    return repeat_count;
}

static flow_color_t flow_service_apply_contrast(flow_color_t color, bool soft_mode)
{
    int avg;
    int factor_pct;

    avg = ((int)color.r + (int)color.g + (int)color.b) / 3;
    factor_pct = soft_mode ? 110 : 128;

    color.r = flow_clamp_u8_from_int(avg + (((int)color.r - avg) * factor_pct) / 100);
    color.g = flow_clamp_u8_from_int(avg + (((int)color.g - avg) * factor_pct) / 100);
    color.b = flow_clamp_u8_from_int(avg + (((int)color.b - avg) * factor_pct) / 100);
    return color;
}

static flow_color_t flow_service_apply_saturation(flow_color_t color, uint8_t saturation_pct)
{
    int avg;

    avg = ((int)color.r + (int)color.g + (int)color.b) / 3;
    color.r = flow_clamp_u8_from_int(avg + (((int)color.r - avg) * (int)saturation_pct) / 100);
    color.g = flow_clamp_u8_from_int(avg + (((int)color.g - avg) * (int)saturation_pct) / 100);
    color.b = flow_clamp_u8_from_int(avg + (((int)color.b - avg) * (int)saturation_pct) / 100);
    return color;
}

static uint16_t flow_service_compute_step_delta(uint8_t speed)
{
    uint16_t delta = (uint16_t)(speed / FLOW_SPEED_DIVISOR);

    if (delta < FLOW_MIN_MOVEMENT_STEP)
    {
        delta = FLOW_MIN_MOVEMENT_STEP;
    }

    return delta;
}

static void flow_service_apply_palette(flow_service_t *service, flow_preset_t preset)
{
    if (service == NULL)
    {
        return;
    }

    flow_service_clear_palette(service);

    switch (preset)
    {
        case FLOW_PRESET_WARM_AMBIENT:
            flow_service_set_palette_color(service, 0u, 255u, 160u, 64u);
            flow_service_set_palette_color(service, 1u, 255u, 190u, 96u);
            flow_service_set_palette_color(service, 2u, 255u, 220u, 180u);
            service->repeat_count = 1u;
            service->speed = FLOW_SPEED_SLOW_VALUE;
            service->brightness = (uint8_t)((40u * 255u) / 100u);
            break;
        case FLOW_PRESET_SUNSET:
            flow_service_set_palette_color(service, 0u, 255u, 120u, 40u);
            flow_service_set_palette_color(service, 1u, 255u, 90u, 90u);
            flow_service_set_palette_color(service, 2u, 255u, 160u, 120u);
            flow_service_set_palette_color(service, 3u, 180u, 80u, 140u);
            service->repeat_count = 2u;
            service->speed = FLOW_SPEED_SLOW_VALUE;
            service->brightness = (uint8_t)((55u * 255u) / 100u);
            break;
        case FLOW_PRESET_OCEAN:
            flow_service_set_palette_color(service, 0u, 32u, 96u, 255u);
            flow_service_set_palette_color(service, 1u, 0u, 160u, 220u);
            flow_service_set_palette_color(service, 2u, 0u, 60u, 160u);
            service->repeat_count = 1u;
            service->speed = FLOW_SPEED_SLOW_VALUE;
            service->brightness = (uint8_t)((50u * 255u) / 100u);
            break;
        case FLOW_PRESET_AURORA:
            flow_service_set_palette_color(service, 0u, 40u, 255u, 180u);
            flow_service_set_palette_color(service, 1u, 40u, 180u, 255u);
            flow_service_set_palette_color(service, 2u, 120u, 80u, 255u);
            flow_service_set_palette_color(service, 3u, 60u, 220u, 140u);
            service->repeat_count = 2u;
            service->speed = FLOW_SPEED_MEDIUM_VALUE;
            service->brightness = (uint8_t)((45u * 255u) / 100u);
            break;
        case FLOW_PRESET_LAVENDER:
            flow_service_set_palette_color(service, 0u, 180u, 120u, 255u);
            flow_service_set_palette_color(service, 1u, 220u, 180u, 255u);
            flow_service_set_palette_color(service, 2u, 140u, 160u, 255u);
            service->repeat_count = 1u;
            service->speed = FLOW_SPEED_SLOW_VALUE;
            service->brightness = (uint8_t)((45u * 255u) / 100u);
            break;
        case FLOW_PRESET_SOFT_RAINBOW:
        default:
            flow_service_set_palette_color(service, 0u, 255u, 90u, 90u);
            flow_service_set_palette_color(service, 1u, 255u, 180u, 80u);
            flow_service_set_palette_color(service, 2u, 180u, 255u, 90u);
            flow_service_set_palette_color(service, 3u, 90u, 220u, 220u);
            flow_service_set_palette_color(service, 4u, 120u, 120u, 255u);
            flow_service_set_palette_color(service, 5u, 220u, 120u, 220u);
            service->repeat_count = 2u;
            service->speed = FLOW_SPEED_MEDIUM_VALUE;
            service->brightness = (uint8_t)((55u * 255u) / 100u);
            break;
    }
}


static uint8_t flow_service_apply_luma_wave(const flow_service_t *service, uint8_t base_brightness)
{
    uint8_t phase;
    uint8_t triangle;
    uint8_t amplitude_pct;
    uint8_t min_brightness_pct;
    int brightness_pct;
    int offset_pct;

    if (service == NULL)
    {
        return base_brightness;
    }

    phase = (uint8_t)((service->step / FLOW_LUMA_WAVE_PHASE_DIVISOR) & 0xFFu);
    triangle = (phase <= 127u) ? phase : (uint8_t)(255u - phase);

    amplitude_pct = service->soft_mode ? FLOW_LUMA_WAVE_AMPLITUDE_SOFT_PCT : FLOW_LUMA_WAVE_AMPLITUDE_PCT;
    min_brightness_pct = service->soft_mode ? FLOW_LUMA_WAVE_SOFT_MIN_BRIGHTNESS_PCT : FLOW_LUMA_WAVE_MIN_BRIGHTNESS_PCT;

    brightness_pct = ((int)base_brightness * 100) / 255;
    offset_pct = ((int)amplitude_pct * (((int)triangle * 2) - 127)) / 127;
    brightness_pct += offset_pct;

    if (brightness_pct < (int)min_brightness_pct)
    {
        brightness_pct = (int)min_brightness_pct;
    }
    if (brightness_pct > 100)
    {
        brightness_pct = 100;
    }

    return (uint8_t)((brightness_pct * 255) / 100);
}

static uint8_t flow_service_apply_soft_breath(flow_service_t *service, uint8_t base_brightness)
{
    uint32_t now_ms;
    uint8_t phase;
    uint8_t triangle;
    int offset_pct;
    int brightness_pct;

    if (service == NULL || !service->soft_mode)
    {
        return base_brightness;
    }

    now_ms = to_ms_since_boot(get_absolute_time());
    if ((now_ms - service->soft_breath_last_ms) >= FLOW_SOFT_MODE_BREATH_INTERVAL_MS)
    {
        service->soft_breath_last_ms = now_ms;
        if (service->soft_breath_forward)
        {
            if (service->soft_breath_phase >= 100u)
            {
                service->soft_breath_phase = 100u;
                service->soft_breath_forward = false;
            }
            else
            {
                service->soft_breath_phase = (uint8_t)(service->soft_breath_phase + 4u);
                if (service->soft_breath_phase > 100u)
                {
                    service->soft_breath_phase = 100u;
                }
            }
        }
        else
        {
            if (service->soft_breath_phase == 0u)
            {
                service->soft_breath_forward = true;
            }
            else if (service->soft_breath_phase <= 4u)
            {
                service->soft_breath_phase = 0u;
                service->soft_breath_forward = true;
            }
            else
            {
                service->soft_breath_phase = (uint8_t)(service->soft_breath_phase - 4u);
            }
        }
    }

    phase = service->soft_breath_phase;
    triangle = (phase <= 50u) ? phase : (uint8_t)(100u - phase);
    offset_pct = ((int)FLOW_SOFT_MODE_BREATH_AMPLITUDE_PCT * ((int)triangle * 2 - 50)) / 50;
    brightness_pct = ((int)base_brightness * 100) / 255;
    brightness_pct += offset_pct;
    if (brightness_pct < 1)
    {
        brightness_pct = 1;
    }
    if (brightness_pct > 100)
    {
        brightness_pct = 100;
    }

    return (uint8_t)((brightness_pct * 255) / 100);
}

const char *flow_service_preset_to_string(flow_preset_t preset)
{
    switch (preset)
    {
        case FLOW_PRESET_WARM_AMBIENT: return "WARM_AMBIENT";
        case FLOW_PRESET_SUNSET:       return "SUNSET";
        case FLOW_PRESET_OCEAN:        return "OCEAN";
        case FLOW_PRESET_AURORA:       return "AURORA";
        case FLOW_PRESET_LAVENDER:     return "LAVENDER";
        case FLOW_PRESET_SOFT_RAINBOW: return "SOFT_RAINBOW";
        default:                       return "UNKNOWN";
    }
}

const char *flow_preset_to_string(flow_preset_t preset)
{
    return flow_service_preset_to_string(preset);
}

const char *flow_service_speed_to_string(uint8_t speed)
{
    if (speed <= FLOW_SPEED_SLOW_VALUE)
    {
        return "SLOW";
    }
    if (speed <= FLOW_SPEED_MEDIUM_VALUE)
    {
        return "MEDIUM";
    }
    return "FAST";
}

bool flow_service_soft_mode_supported(flow_preset_t preset)
{
    return preset != FLOW_PRESET_SOFT_RAINBOW;
}

bool flow_service_init(flow_service_t *service, led_service_t *led_service)
{
    if (service == NULL || led_service == NULL)
    {
        return false;
    }

    service->led_service = led_service;
    service->enabled = true;
    service->preset = FLOW_PRESET_WARM_AMBIENT;
    service->brightness = FLOW_DEFAULT_BRIGHTNESS;
    service->speed = FLOW_DEFAULT_SPEED;
    service->soft_mode = false;
    service->step = 0u;
    service->last_update_ms = 0u;
    service->soft_breath_phase = 50u;
    service->soft_breath_forward = true;
    service->soft_breath_last_ms = 0u;
    flow_service_clear_palette(service);

    flow_service_apply_palette(service, service->preset);
    return true;
}

bool flow_service_is_enabled(const flow_service_t *service)
{
    return (service != NULL) && service->enabled;
}

bool flow_service_set_preset(flow_service_t *service, flow_preset_t preset)
{
    if (service == NULL)
    {
        return false;
    }

    service->preset = preset;
    service->enabled = true;
    service->step = 0u;
    flow_service_apply_palette(service, preset);

    if (service->soft_mode && !flow_service_soft_mode_supported(preset))
    {
        service->soft_mode = false;
    }

    return true;
}

flow_preset_t flow_service_get_preset(const flow_service_t *service)
{
    if (service == NULL)
    {
        return FLOW_PRESET_WARM_AMBIENT;
    }

    return service->preset;
}

bool flow_service_set_speed(flow_service_t *service, uint8_t speed)
{
    if (service == NULL)
    {
        return false;
    }

    if (speed == 0u)
    {
        speed = FLOW_SPEED_SLOW_VALUE;
    }

    if (service->soft_mode && speed > FLOW_SPEED_MEDIUM_VALUE)
    {
        speed = FLOW_SPEED_MEDIUM_VALUE;
    }

    service->enabled = true;
    service->speed = speed;
    return true;
}

uint8_t flow_service_get_speed(const flow_service_t *service)
{
    return (service != NULL) ? service->speed : 0u;
}

bool flow_service_set_brightness(flow_service_t *service, uint8_t brightness)
{
    if (service == NULL)
    {
        return false;
    }

    if (service->soft_mode)
    {
        uint8_t limit = (service->preset == FLOW_PRESET_AURORA) ? FLOW_SOFT_MODE_BRIGHTNESS_MAX_AURORA : FLOW_SOFT_MODE_BRIGHTNESS_MAX;
        if (brightness > limit)
        {
            brightness = limit;
        }
    }

    service->enabled = true;
    service->brightness = brightness;
    return true;
}

uint8_t flow_service_get_brightness(const flow_service_t *service)
{
    return (service != NULL) ? service->brightness : 0u;
}

bool flow_service_set_soft_mode(flow_service_t *service, bool enabled)
{
    if (service == NULL)
    {
        return false;
    }

    if (enabled && !flow_service_soft_mode_supported(service->preset))
    {
        return false;
    }

    service->soft_mode = enabled;
    if (service->soft_mode)
    {
        if (service->speed > FLOW_SPEED_MEDIUM_VALUE)
        {
            service->speed = FLOW_SPEED_MEDIUM_VALUE;
        }
        {
            uint8_t limit = (service->preset == FLOW_PRESET_AURORA) ? FLOW_SOFT_MODE_BRIGHTNESS_MAX_AURORA : FLOW_SOFT_MODE_BRIGHTNESS_MAX;
            if (service->brightness > limit)
            {
                service->brightness = limit;
            }
        }
        service->soft_breath_phase = 50u;
        service->soft_breath_forward = true;
    }
    return true;
}

bool flow_service_get_soft_mode(const flow_service_t *service)
{
    return (service != NULL) && service->soft_mode;
}

bool flow_service_set_gradient(flow_service_t *service,
                               uint8_t r1, uint8_t g1, uint8_t b1,
                               uint8_t r2, uint8_t g2, uint8_t b2,
                               uint8_t r3, uint8_t g3, uint8_t b3)
{
    if (service == NULL)
    {
        return false;
    }

    service->enabled = true;
    service->preset = FLOW_PRESET_AURORA;
    flow_service_clear_palette(service);
    flow_service_set_palette_color(service, 0u, r1, g1, b1);
    flow_service_set_palette_color(service, 1u, r2, g2, b2);
    flow_service_set_palette_color(service, 2u, r3, g3, b3);
    service->repeat_count = 1u;
    service->step = 0u;

    if (service->soft_mode && !flow_service_soft_mode_supported(service->preset))
    {
        service->soft_mode = false;
    }

    return true;
}

void flow_service_get_gradient(const flow_service_t *service,
                               uint8_t *r1, uint8_t *g1, uint8_t *b1,
                               uint8_t *r2, uint8_t *g2, uint8_t *b2,
                               uint8_t *r3, uint8_t *g3, uint8_t *b3)
{
    flow_color_t c1 = flow_service_get_palette_color(service, 0u);
    flow_color_t c2 = flow_service_get_palette_color(service, 1u);
    flow_color_t c3 = flow_service_get_palette_color(service, 2u);

    if (r1 != NULL) *r1 = c1.r;
    if (g1 != NULL) *g1 = c1.g;
    if (b1 != NULL) *b1 = c1.b;
    if (r2 != NULL) *r2 = c2.r;
    if (g2 != NULL) *g2 = c2.g;
    if (b2 != NULL) *b2 = c2.b;
    if (r3 != NULL) *r3 = c3.r;
    if (g3 != NULL) *g3 = c3.g;
    if (b3 != NULL) *b3 = c3.b;
}

bool flow_service_update(flow_service_t *service)
{
    uint32_t now_ms;
    uint16_t led_count;
    uint8_t effective_brightness;
    uint8_t repeat_count;
    uint16_t i;

    if (service == NULL || service->led_service == NULL || service->led_service->device == NULL)
    {
        return false;
    }

    now_ms = to_ms_since_boot(get_absolute_time());
    if ((now_ms - service->last_update_ms) < FLOW_UPDATE_INTERVAL_MS)
    {
        return true;
    }
    service->last_update_ms = now_ms;

    effective_brightness = flow_service_apply_soft_breath(service, service->brightness);
    effective_brightness = flow_service_apply_luma_wave(service, effective_brightness);
    if (!led_service_set_brightness(service->led_service, effective_brightness))
    {
        return false;
    }

    led_count = service->led_service->device->led_count;
    if (led_count == 0u)
    {
        return false;
    }

    repeat_count = flow_service_effective_repeat_count(service, led_count);

    for (i = 0u; i < led_count; i++)
    {
        uint8_t phase = compute_palette_phase(service->step, i, led_count, repeat_count);
        flow_color_t color = gradient_palette_color(service, phase);

        color = flow_service_apply_contrast(color, service->soft_mode);
        if (service->soft_mode && service->preset == FLOW_PRESET_AURORA)
        {
            color = flow_service_apply_saturation(color, 85u);
        }

        if (!led_service_set_pixel(service->led_service, i, color.r, color.g, color.b))
        {
            return false;
        }

        if (i == 0u)
        {
            led_service_set_state(service->led_service,
                                  color.r,
                                  color.g,
                                  color.b,
                                  effective_brightness);
        }
    }

    led_service_show(service->led_service);
    service->step = (uint16_t)(service->step + flow_service_compute_step_delta(service->speed));
    return true;
}
