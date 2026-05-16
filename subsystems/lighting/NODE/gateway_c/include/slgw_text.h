#ifndef SLGW_TEXT_H
#define SLGW_TEXT_H

#include <stdbool.h>
#include <stddef.h>

int slgw_copy_text(char *dst, size_t dst_size, const char *src);
int slgw_copy_text_trimmed(char *dst, size_t dst_size, const char *src);
int slgw_copy_upper_text(char *dst, size_t dst_size, const char *src);
int slgw_copy_lower_text(char *dst, size_t dst_size, const char *src);
int slgw_format(char *dst, size_t dst_size, const char *fmt, ...);
int slgw_parse_int(const char *text, int *out_value);
int slgw_parse_unsigned(const char *text, unsigned int *out_value);
const char *slgw_bool_text(bool value);
size_t slgw_strnlen_safe(const char *text, size_t max_len);

#endif
