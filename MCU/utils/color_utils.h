#ifndef COLOR_UTILS_H
#define COLOR_UTILS_H

#include <stdbool.h>
#include <stdint.h>

void color_utils_apply_tone_bias(int tone_bias, uint8_t *r, uint8_t *g, uint8_t *b);
bool color_utils_kelvin_to_rgb(int kelvin, uint8_t *r, uint8_t *g, uint8_t *b);
bool color_utils_is_valid_kelvin(int kelvin);

#endif
