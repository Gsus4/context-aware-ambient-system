#include "slgw_text.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t slgw_strnlen_safe(const char *text, size_t max_len) {
    size_t idx;
    if (text == NULL) {
        return 0;
    }
    for (idx = 0; idx < max_len; ++idx) {
        if (text[idx] == '\0') {
            return idx;
        }
    }
    return max_len;
}

int slgw_copy_text(char *dst, size_t dst_size, const char *src) {
    size_t length;
    if (dst == NULL || dst_size == 0) {
        return -1;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return 0;
    }
    length = strlen(src);
    if (length + 1 > dst_size) {
        dst[0] = '\0';
        return -1;
    }
    memcpy(dst, src, length + 1);
    return 0;
}

int slgw_copy_text_trimmed(char *dst, size_t dst_size, const char *src) {
    const char *start;
    const char *end;
    size_t length;
    if (dst == NULL || dst_size == 0) {
        return -1;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return 0;
    }
    start = src;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    length = (size_t)(end - start);
    if (length + 1 > dst_size) {
        dst[0] = '\0';
        return -1;
    }
    memcpy(dst, start, length);
    dst[length] = '\0';
    return 0;
}


static int slgw_copy_case_text(char *dst, size_t dst_size, const char *src, int make_upper) {
    size_t idx;
    if (slgw_copy_text(dst, dst_size, src) != 0) {
        return -1;
    }
    for (idx = 0; dst[idx] != '\0'; ++idx) {
        dst[idx] = (char)(make_upper ? toupper((unsigned char)dst[idx]) : tolower((unsigned char)dst[idx]));
    }
    return 0;
}

int slgw_copy_upper_text(char *dst, size_t dst_size, const char *src) {
    return slgw_copy_case_text(dst, dst_size, src, 1);
}

int slgw_copy_lower_text(char *dst, size_t dst_size, const char *src) {
    return slgw_copy_case_text(dst, dst_size, src, 0);
}
int slgw_format(char *dst, size_t dst_size, const char *fmt, ...) {
    va_list args;
    int written;
    if (dst == NULL || dst_size == 0 || fmt == NULL) {
        return -1;
    }
    va_start(args, fmt);
    written = vsnprintf(dst, dst_size, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= dst_size) {
        dst[0] = '\0';
        return -1;
    }
    return 0;
}

int slgw_parse_int(const char *text, int *out_value) {
    char *endptr = NULL;
    long value;
    if (text == NULL || out_value == NULL || text[0] == '\0') {
        return -1;
    }
    value = strtol(text, &endptr, 10);
    if (endptr == NULL || *endptr != '\0') {
        return -1;
    }
    *out_value = (int)value;
    return 0;
}

int slgw_parse_unsigned(const char *text, unsigned int *out_value) {
    char *endptr = NULL;
    unsigned long value;
    if (text == NULL || out_value == NULL || text[0] == '\0') {
        return -1;
    }
    value = strtoul(text, &endptr, 10);
    if (endptr == NULL || *endptr != '\0') {
        return -1;
    }
    *out_value = (unsigned int)value;
    return 0;
}

const char *slgw_bool_text(bool value) {
    return value ? "true" : "false";
}
