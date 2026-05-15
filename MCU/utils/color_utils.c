#include "color_utils.h"
#include <stddef.h>

#include "config.h"

static int color_utils_clamp_int(int value, int min_value, int max_value)
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

static uint8_t color_utils_clamp_u8(int value)
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

void color_utils_apply_tone_bias(int tone_bias, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int rr;
    int gg;
    int bb;
    int bias;

    if (r == NULL || g == NULL || b == NULL)
    {
        return;
    }

    bias = color_utils_clamp_int(tone_bias, LIGHT_NODE_TONE_BIAS_MIN, LIGHT_NODE_TONE_BIAS_MAX);
    rr = *r;
    gg = *g;
    bb = *b;

    if (bias > 0)
    {
        rr -= bias * 16;
        gg += bias * 6;
        bb += bias * 20;
    }
    else if (bias < 0)
    {
        bias = -bias;
        rr += bias * 16;
        gg -= bias * 4;
        bb -= bias * 20;
    }

    *r = color_utils_clamp_u8(rr);
    *g = color_utils_clamp_u8(gg);
    *b = color_utils_clamp_u8(bb);
}


bool color_utils_is_valid_kelvin(int kelvin)
{
    return kelvin >= COLOR_TEMP_K_MIN && kelvin <= COLOR_TEMP_K_MAX;
}

bool color_utils_kelvin_to_rgb(int kelvin, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int k;
    int segment;
    int ratio;
    int rr;
    int gg;
    int bb;

    /*
     * 以固定點線性插值近似 2700K~6500K 的白光 RGB。
     * 目的不是精密 CCT 模型，而是讓 Web 色溫滑桿有穩定、可預期的暖冷控制。
     */
    static const int table_k[] = {2700, 3000, 3900, 4500, 5500, 6500};
    static const uint8_t table_rgb[][3] = {
        {255u, 169u,  87u},
        {255u, 180u, 107u},
        {255u, 213u, 171u},
        {255u, 228u, 206u},
        {255u, 242u, 237u},
        {255u, 249u, 253u}
    };

    if (r == NULL || g == NULL || b == NULL)
    {
        return false;
    }
    if (!color_utils_is_valid_kelvin(kelvin))
    {
        return false;
    }

    k = kelvin;
    for (segment = 0; segment < 5; ++segment)
    {
        if (k <= table_k[segment + 1])
        {
            break;
        }
    }

    ratio = ((k - table_k[segment]) * 1000) / (table_k[segment + 1] - table_k[segment]);
    rr = table_rgb[segment][0] + ((int)table_rgb[segment + 1][0] - (int)table_rgb[segment][0]) * ratio / 1000;
    gg = table_rgb[segment][1] + ((int)table_rgb[segment + 1][1] - (int)table_rgb[segment][1]) * ratio / 1000;
    bb = table_rgb[segment][2] + ((int)table_rgb[segment + 1][2] - (int)table_rgb[segment][2]) * ratio / 1000;

    *r = color_utils_clamp_u8(rr);
    *g = color_utils_clamp_u8(gg);
    *b = color_utils_clamp_u8(bb);
    return true;
}
