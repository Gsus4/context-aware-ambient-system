#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>

#include "smartlight_ioctl.h"

#define DEVICE_NAME              "smartlight0"
#define CLASS_NAME               "smartlight"
#define DRIVER_TAG               "smartlight_drv"
#define SMARTLIGHT_QUEUE_DEPTH   64
#define SMARTLIGHT_READ_SLEEP_MS 10

static char *uart_path = "/dev/serial0";
module_param(uart_path, charp, 0644);
MODULE_PARM_DESC(uart_path, "UART tty device path, e.g. /dev/serial0 or /dev/ttyAMA0");

static bool debug_rx = true;
module_param(debug_rx, bool, 0644);
MODULE_PARM_DESC(debug_rx, "Enable RX parser debug logs");

struct smartlight_dev {
    dev_t devno;
    struct cdev cdev;
    struct class *class;
    struct device *device;

    struct file *uart_filp;
    struct task_struct *rx_thread;

    struct mutex state_lock;
    spinlock_t rx_lock;
    wait_queue_head_t read_wq;

    smartlight_frame_t rx_queue[SMARTLIGHT_QUEUE_DEPTH];
    unsigned int rx_head;
    unsigned int rx_tail;
    unsigned int rx_count;

    smartlight_frame_t last_rx_frame;
    smartlight_frame_t last_tx_frame;
    smartlight_status_v4_t last_status;
    smartlight_stats_t stats;
    bool shutting_down;
};

static struct smartlight_dev g_dev;

/*
 * Legacy user-space compatibility
 * Older Python helper used a different status struct layout and
 * a different ioctl number for CLEAR_BUFFERS. Keep these aliases so
 * existing scripts continue to work.
 */
struct smartlight_status_v3_legacy {
    __u32 session_id;
    __u32 seq;
    __u32 uptime_ms;

    __u8 report_enabled;
    __u8 flow_enabled;

    __u8 manual_r;
    __u8 manual_g;
    __u8 manual_b;
    __u8 manual_brightness;

    __u8 flow_speed;
    __u8 flow_brightness;

    __u8 flow_c1_r;
    __u8 flow_c1_g;
    __u8 flow_c1_b;
    __u8 flow_c2_r;
    __u8 flow_c2_g;
    __u8 flow_c2_b;
    __u8 flow_c3_r;
    __u8 flow_c3_g;
    __u8 flow_c3_b;

    char system_state[SMARTLIGHT_STATE_LEN];
    char mode[SMARTLIGHT_MODE_LEN];
    char scene[SMARTLIGHT_SCENE_LEN];
    char flow_preset[SMARTLIGHT_PRESET_LEN];
    char reserved[32];
};

#define SL_IOC_GET_LAST_STATUS_LEGACY _IOR(SMARTLIGHT_IOCTL_MAGIC, 0x01, struct smartlight_status_v3_legacy)
#define SL_IOC_CLEAR_BUFFERS_LEGACY   _IO(SMARTLIGHT_IOCTL_MAGIC,  0x03)

static void sl_fill_status_v3_from_v4(const smartlight_status_v4_t *src,
                                   smartlight_status_v3_t *dst)
{
    memset(dst, 0, sizeof(*dst));
    strscpy(dst->system_state, "RUNNING", sizeof(dst->system_state));
    strscpy(dst->mode, src->active_mode, sizeof(dst->mode));
    strscpy(dst->scene, src->active_scene, sizeof(dst->scene));
    strscpy(dst->flow_preset, src->flow_preset, sizeof(dst->flow_preset));
    dst->uptime_ms = src->uptime_ms;
    dst->report_enabled = 0;
    dst->flow_enabled = src->flow_enabled;
    dst->manual_brightness = (u8)clamp_t(int, src->brightness_pct, 0, 100);
    if (!strcmp(src->flow_speed, "fast"))
        dst->flow_speed = 128;
    else if (!strcmp(src->flow_speed, "medium"))
        dst->flow_speed = 80;
    else
        dst->flow_speed = 40;
    dst->flow_brightness = (u8)clamp_t(int, src->flow_brightness_pct, 0, 100);
}

static void sl_fill_legacy_status(const smartlight_status_v4_t *src,
                                  struct smartlight_status_v3_legacy *dst)
{
    smartlight_status_v3_t compat;
    sl_fill_status_v3_from_v4(src, &compat);
    memset(dst, 0, sizeof(*dst));

    dst->session_id = compat.session_id;
    dst->seq = compat.seq;
    dst->uptime_ms = compat.uptime_ms;
    dst->report_enabled = compat.report_enabled;
    dst->flow_enabled = compat.flow_enabled;
    dst->manual_r = compat.manual_r;
    dst->manual_g = compat.manual_g;
    dst->manual_b = compat.manual_b;
    dst->manual_brightness = compat.manual_brightness;
    dst->flow_speed = compat.flow_speed;
    dst->flow_brightness = compat.flow_brightness;
    dst->flow_c1_r = compat.flow_c1_r;
    dst->flow_c1_g = compat.flow_c1_g;
    dst->flow_c1_b = compat.flow_c1_b;
    dst->flow_c2_r = compat.flow_c2_r;
    dst->flow_c2_g = compat.flow_c2_g;
    dst->flow_c2_b = compat.flow_c2_b;
    dst->flow_c3_r = compat.flow_c3_r;
    dst->flow_c3_g = compat.flow_c3_g;
    dst->flow_c3_b = compat.flow_c3_b;
    strscpy(dst->system_state, compat.system_state, sizeof(dst->system_state));
    strscpy(dst->mode, compat.mode, sizeof(dst->mode));
    strscpy(dst->scene, compat.scene, sizeof(dst->scene));
    strscpy(dst->flow_preset, compat.flow_preset, sizeof(dst->flow_preset));
}

static void sl_status_set_defaults_v3(smartlight_status_v3_t *st)
{
    memset(st, 0, sizeof(*st));
    strscpy(st->system_state, "UNKNOWN", sizeof(st->system_state));
    strscpy(st->mode, "UNKNOWN", sizeof(st->mode));
    strscpy(st->scene, "NONE", sizeof(st->scene));
    strscpy(st->flow_preset, "NONE", sizeof(st->flow_preset));
}

static void sl_status_set_defaults_v4(smartlight_status_v4_t *st)
{
    memset(st, 0, sizeof(*st));
    strscpy(st->active_mode, "unknown", sizeof(st->active_mode));
    strscpy(st->active_scene, "none", sizeof(st->active_scene));
    strscpy(st->flow_preset, "none", sizeof(st->flow_preset));
    strscpy(st->flow_speed, "slow", sizeof(st->flow_speed));
    strscpy(st->breathing_speed, "slow", sizeof(st->breathing_speed));
    strscpy(st->breathing_strength, "low", sizeof(st->breathing_strength));
}

static void sl_status_v3_to_v4(const smartlight_status_v3_t *src,
                               smartlight_status_v4_t *dst)
{
    sl_status_set_defaults_v4(dst);
    dst->uptime_ms = src->uptime_ms;
    strscpy(dst->active_mode, src->mode, sizeof(dst->active_mode));
    strscpy(dst->active_scene, src->scene, sizeof(dst->active_scene));
    strscpy(dst->flow_preset, src->flow_preset, sizeof(dst->flow_preset));
    dst->brightness_pct = src->manual_brightness;
    dst->flow_enabled = src->flow_enabled;
    dst->flow_brightness_pct = src->flow_brightness;
    if (src->flow_speed >= 128)
        strscpy(dst->flow_speed, "fast", sizeof(dst->flow_speed));
    else if (src->flow_speed >= 80)
        strscpy(dst->flow_speed, "medium", sizeof(dst->flow_speed));
    else
        strscpy(dst->flow_speed, "slow", sizeof(dst->flow_speed));
}

static void sl_trim_token(char *s)
{
    char *start;
    size_t len;

    if (!s)
        return;

    start = s;
    while (*start == ' ' || *start == '\t')
        start++;

    if (start != s)
        memmove(s, start, strlen(start) + 1);

    len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[len - 1] = '\0';
        len--;
    }
}

static void sl_debug_dump_parts(char *parts[], int count)
{
    int i;

    if (!debug_rx)
        return;

    pr_info(DRIVER_TAG ": parse fields=%d\n", count);
    for (i = 0; i < count; i++)
        pr_info(DRIVER_TAG ": part[%d]=\"%s\"\n", i, parts[i] ? parts[i] : "(null)");
}

static int sl_bool_on_off(const char *s, u8 *out)
{
    if (!s || !out)
        return -EINVAL;

    if (!strcmp(s, "ON")) {
        *out = 1;
        return 0;
    }
    if (!strcmp(s, "OFF")) {
        *out = 0;
        return 0;
    }

    return -EINVAL;
}

static int sl_parse_u8(const char *s, u8 *out)
{
    unsigned int v;
    int ret;

    if (!s || !out)
        return -EINVAL;

    ret = kstrtouint(s, 10, &v);
    if (ret)
        return ret;
    if (v > 255)
        return -ERANGE;

    *out = (u8)v;
    return 0;
}

static int sl_parse_u32(const char *s, u32 *out)
{
    unsigned int v;
    int ret;

    if (!s || !out)
        return -EINVAL;

    ret = kstrtouint(s, 10, &v);
    if (ret)
        return ret;

    *out = (u32)v;
    return 0;
}

/* Parse decimal string into x100 fixed point, e.g. "123.45" -> 12345 */
static int sl_parse_decimal_x100(const char *s, u32 *out)
{
    char buf[32];
    char *dot;
    char *frac;
    unsigned int whole = 0;
    unsigned int frac_v = 0;
    int ret;
    size_t len;

    if (!s || !out)
        return -EINVAL;

    len = strnlen(s, sizeof(buf));
    if (len == 0 || len >= sizeof(buf))
        return -EINVAL;

    memcpy(buf, s, len);
    buf[len] = '\0';

    dot = strchr(buf, '.');
    if (!dot) {
        ret = kstrtouint(buf, 10, &whole);
        if (ret)
            return ret;
        *out = whole * 100;
        return 0;
    }

    *dot = '\0';
    frac = dot + 1;

    if (buf[0]) {
        ret = kstrtouint(buf, 10, &whole);
        if (ret)
            return ret;
    }

    if (frac[0]) {
        if (frac[1]) {
            char tmp[3];
            tmp[0] = frac[0];
            tmp[1] = frac[1];
            tmp[2] = '\0';
            ret = kstrtouint(tmp, 10, &frac_v);
        } else {
            char tmp[3];
            tmp[0] = frac[0];
            tmp[1] = '0';
            tmp[2] = '\0';
            ret = kstrtouint(tmp, 10, &frac_v);
        }
        if (ret)
            return ret;
    }

    *out = whole * 100 + frac_v;
    return 0;
}

static void sl_cache_frame(smartlight_frame_t *dst, const char *src, size_t len)
{
    size_t n = min(len, (size_t)(SMARTLIGHT_MAX_FRAME - 1));

    memset(dst, 0, sizeof(*dst));
    memcpy(dst->data, src, n);
    dst->data[n] = '\0';
    dst->length = (u32)n;
}

static int sl_parse_state_status_v3(const char *line, smartlight_status_v3_t *st)
{
    char buf[SMARTLIGHT_MAX_FRAME];
    char *p = buf;
    char *tok;
    char *parts[32];
    int i = 0;
    int ret;

    if (!line || !st)
        return -EINVAL;

    strscpy(buf, line, sizeof(buf));

    while ((tok = strsep(&p, ",")) != NULL && i < ARRAY_SIZE(parts)) {
        sl_trim_token(tok);
        parts[i++] = tok;
    }

    if (debug_rx)
        pr_info(DRIVER_TAG ": RX line=\"%s\"\n", line);

    if (i < 15) {
        if (debug_rx) {
            pr_err(DRIVER_TAG ": parse fail: too few fields (%d)\n", i);
            sl_debug_dump_parts(parts, i);
        }
        return -EINVAL;
    }

    if (strcmp(parts[0], "STATE") || strcmp(parts[1], "STATUS") || strcmp(parts[2], "V3")) {
        if (debug_rx) {
            pr_err(DRIVER_TAG ": parse fail: not STATE,STATUS,V3\n");
            sl_debug_dump_parts(parts, i);
        }
        return -EINVAL;
    }

    while (i < 28) {
        if (i == 15)
            parts[i++] = "OFF";
        else if (i == 16)
            parts[i++] = "NONE";
        else
            parts[i++] = "0";
    }

    sl_status_set_defaults_v3(st);

    ret = sl_parse_u32(parts[3], &st->session_id);
    if (ret) st->session_id = 0;

    ret = sl_parse_u32(parts[4], &st->seq);
    if (ret) st->seq = 0;

    ret = sl_parse_u32(parts[5], &st->uptime_ms);
    if (ret) st->uptime_ms = 0;

    if (parts[6] && parts[6][0])
        strscpy(st->system_state, parts[6], sizeof(st->system_state));

    ret = sl_bool_on_off(parts[7], &st->report_enabled);
    if (ret)
        st->report_enabled = 0;

    if (parts[8] && parts[8][0])
        strscpy(st->mode, parts[8], sizeof(st->mode));

    if (parts[9] && parts[9][0])
        strscpy(st->scene, parts[9], sizeof(st->scene));

    ret = sl_parse_u8(parts[10], &st->manual_r);
    if (ret) st->manual_r = 0;
    ret = sl_parse_u8(parts[11], &st->manual_g);
    if (ret) st->manual_g = 0;
    ret = sl_parse_u8(parts[12], &st->manual_b);
    if (ret) st->manual_b = 0;
    ret = sl_parse_u8(parts[13], &st->manual_brightness);
    if (ret) st->manual_brightness = 0;

    ret = sl_parse_decimal_x100(parts[14], &st->lux_x100);
    if (ret)
        st->lux_x100 = 0;

    ret = sl_bool_on_off(parts[15], &st->flow_enabled);
    if (ret)
        st->flow_enabled = 0;

    if (parts[16] && parts[16][0])
        strscpy(st->flow_preset, parts[16], sizeof(st->flow_preset));

    ret = sl_parse_u8(parts[17], &st->flow_speed);
    if (ret) st->flow_speed = 0;
    ret = sl_parse_u8(parts[18], &st->flow_brightness);
    if (ret) st->flow_brightness = 0;

    ret = sl_parse_u8(parts[19], &st->flow_c1_r);
    if (ret) st->flow_c1_r = 0;
    ret = sl_parse_u8(parts[20], &st->flow_c1_g);
    if (ret) st->flow_c1_g = 0;
    ret = sl_parse_u8(parts[21], &st->flow_c1_b);
    if (ret) st->flow_c1_b = 0;

    ret = sl_parse_u8(parts[22], &st->flow_c2_r);
    if (ret) st->flow_c2_r = 0;
    ret = sl_parse_u8(parts[23], &st->flow_c2_g);
    if (ret) st->flow_c2_g = 0;
    ret = sl_parse_u8(parts[24], &st->flow_c2_b);
    if (ret) st->flow_c2_b = 0;

    ret = sl_parse_u8(parts[25], &st->flow_c3_r);
    if (ret) st->flow_c3_r = 0;
    ret = sl_parse_u8(parts[26], &st->flow_c3_g);
    if (ret) st->flow_c3_g = 0;
    ret = sl_parse_u8(parts[27], &st->flow_c3_b);
    if (ret) st->flow_c3_b = 0;

    if (debug_rx)
        pr_info(DRIVER_TAG ": parse ok: session=%u seq=%u mode=%s lux_x100=%u flow=%u preset=%s\n",
                st->session_id, st->seq, st->mode, st->lux_x100,
                st->flow_enabled, st->flow_preset);

    return 0;
}

static int sl_parse_state_status_v4(const char *line, smartlight_status_v4_t *st)
{
    char buf[SMARTLIGHT_MAX_FRAME];
    char *p = buf;
    char *tok;
    char *parts[32];
    int i = 0;

    if (!line || !st)
        return -EINVAL;

    strscpy(buf, line, sizeof(buf));
    while ((tok = strsep(&p, ",")) != NULL && i < ARRAY_SIZE(parts)) {
        sl_trim_token(tok);
        parts[i++] = tok;
    }

    if (i != 20)
        return -EINVAL;
    if (strcmp(parts[0], "STATE") || strcmp(parts[1], "STATUS") || strcmp(parts[2], "V4"))
        return -EINVAL;

    sl_status_set_defaults_v4(st);
    if (sl_parse_u32(parts[3], &st->uptime_ms)) st->uptime_ms = 0;
    strscpy(st->active_mode, parts[4], sizeof(st->active_mode));
    strscpy(st->active_scene, parts[5], sizeof(st->active_scene));
    st->scene_modified = !strcmp(parts[6], "ON");
    if (kstrtoint(parts[7], 10, &st->brightness_pct)) st->brightness_pct = 0;
    if (kstrtoint(parts[8], 10, &st->tone_bias)) st->tone_bias = 0;
    st->flow_enabled = !strcmp(parts[9], "ON");
    strscpy(st->flow_preset, parts[10], sizeof(st->flow_preset));
    strscpy(st->flow_speed, parts[11], sizeof(st->flow_speed));
    if (kstrtoint(parts[12], 10, &st->flow_brightness_pct)) st->flow_brightness_pct = 0;
    st->flow_soft_mode = !strcmp(parts[13], "ON");
    st->breathing_enabled = !strcmp(parts[14], "ON");
    strscpy(st->breathing_speed, parts[15], sizeof(st->breathing_speed));
    strscpy(st->breathing_strength, parts[16], sizeof(st->breathing_strength));
    st->manual_override = !strcmp(parts[19], "ON");
    return 0;
}

static int sl_parse_state_status_v5(const char *line, smartlight_status_v5_t *st)
{
    char buf[SMARTLIGHT_MAX_FRAME];
    char *p = buf;
    char *tok;
    char *parts[32];
    int i = 0;

    if (!line || !st)
        return -EINVAL;

    strscpy(buf, line, sizeof(buf));
    while ((tok = strsep(&p, ",")) != NULL && i < ARRAY_SIZE(parts)) {
        sl_trim_token(tok);
        parts[i++] = tok;
    }

    if (i != 18)
        return -EINVAL;
    if (strcmp(parts[0], "STATE") || strcmp(parts[1], "STATUS") || strcmp(parts[2], "V5"))
        return -EINVAL;

    sl_status_set_defaults_v4(st);
    if (sl_parse_u32(parts[3], &st->uptime_ms)) st->uptime_ms = 0;
    strscpy(st->active_mode, parts[4], sizeof(st->active_mode));
    strscpy(st->active_scene, parts[5], sizeof(st->active_scene));
    st->scene_modified = !strcmp(parts[6], "ON");
    if (kstrtoint(parts[7], 10, &st->brightness_pct)) st->brightness_pct = 0;
    if (kstrtoint(parts[8], 10, &st->tone_bias)) st->tone_bias = 0;
    st->flow_enabled = !strcmp(parts[9], "ON");
    strscpy(st->flow_preset, parts[10], sizeof(st->flow_preset));
    strscpy(st->flow_speed, parts[11], sizeof(st->flow_speed));
    if (kstrtoint(parts[12], 10, &st->flow_brightness_pct)) st->flow_brightness_pct = 0;
    st->flow_soft_mode = !strcmp(parts[13], "ON");
    st->breathing_enabled = !strcmp(parts[14], "ON");
    strscpy(st->breathing_speed, parts[15], sizeof(st->breathing_speed));
    strscpy(st->breathing_strength, parts[16], sizeof(st->breathing_strength));
    st->manual_override = !strcmp(parts[17], "ON");
    return 0;
}

static void sl_apply_event_to_status(struct smartlight_dev *dev, const char *line)
{
    char buf[SMARTLIGHT_MAX_FRAME];
    char *p = buf;
    char *tok;
    char *parts[8];
    int i = 0;

    if (!line)
        return;

    strscpy(buf, line, sizeof(buf));
    while ((tok = strsep(&p, ",")) != NULL && i < ARRAY_SIZE(parts)) {
        sl_trim_token(tok);
        parts[i++] = tok;
    }

    if (i < 5 || strcmp(parts[0], "EVENT") != 0)
        return;

    if (!strcmp(parts[1], "MODE_CHANGED")) {
        if (!strncmp(parts[3], "FLOW_", 5)) {
            strscpy(dev->last_status.active_mode, "flow", sizeof(dev->last_status.active_mode));
            dev->last_status.flow_enabled = 1;
        } else if (!strncmp(parts[3], "SCENE_", 6)) {
            strscpy(dev->last_status.active_mode, "scene", sizeof(dev->last_status.active_mode));
            dev->last_status.flow_enabled = 0;
        } else if (!strcmp(parts[3], "STATIC_BREATHING_UPDATED")) {
            strscpy(dev->last_status.active_mode, "static", sizeof(dev->last_status.active_mode));
            dev->last_status.breathing_enabled = 1;
        } else if (!strcmp(parts[3], "TRANSITION_UPDATED")) {
            /* keep mode unchanged */
        } else if (!strcmp(parts[3], "POWER_OFF")) {
            dev->last_status.brightness_pct = 0;
            dev->last_status.flow_enabled = 0;
            dev->last_status.breathing_enabled = 0;
        } else if (!strcmp(parts[3], "AUTO_APPLIED")) {
            strscpy(dev->last_status.active_mode, "auto", sizeof(dev->last_status.active_mode));
            dev->last_status.flow_enabled = 0;
        } else if (!strcmp(parts[3], "STATIC_APPLIED") || !strcmp(parts[3], "MANUAL_APPLIED")) {
            strscpy(dev->last_status.active_mode, "static", sizeof(dev->last_status.active_mode));
            dev->last_status.flow_enabled = 0;
        }
    }
}

static void sl_handle_rx_line(struct smartlight_dev *dev, const char *line, size_t len)
{
    unsigned long flags;
    smartlight_status_v4_t parsed;

    if (!len)
        return;

    mutex_lock(&dev->state_lock);

    sl_cache_frame(&dev->last_rx_frame, line, len);
    dev->stats.rx_frames++;

    if (!strncmp(line, "STATE,STATUS,V4", 15)) {
        if (!sl_parse_state_status_v4(line, &parsed)) {
            memcpy(&dev->last_status, &parsed, sizeof(parsed));
        } else {
            dev->stats.parse_errors++;
            if (debug_rx)
                pr_err(DRIVER_TAG ": STATE V4 parse failed: \"%s\"\n", line);
        }
    } else if (!strncmp(line, "STATE,STATUS,V3", 15)) {
        smartlight_status_v3_t parsed_v3;
        if (!sl_parse_state_status_v3(line, &parsed_v3)) {
            sl_status_v3_to_v4(&parsed_v3, &parsed);
            memcpy(&dev->last_status, &parsed, sizeof(parsed));
        } else {
            dev->stats.parse_errors++;
            if (debug_rx)
                pr_err(DRIVER_TAG ": STATE V3 parse failed: \"%s\"\n", line);
        }
    } else if (!strncmp(line, "EVENT,", 6)) {
        sl_apply_event_to_status(dev, line);
    }

    spin_lock_irqsave(&dev->rx_lock, flags);

    if (dev->rx_count == SMARTLIGHT_QUEUE_DEPTH) {
        dev->rx_tail = (dev->rx_tail + 1) % SMARTLIGHT_QUEUE_DEPTH;
        dev->rx_count--;
        dev->stats.dropped_frames++;
    }

    sl_cache_frame(&dev->rx_queue[dev->rx_head], line, len);
    dev->rx_head = (dev->rx_head + 1) % SMARTLIGHT_QUEUE_DEPTH;
    dev->rx_count++;

    spin_unlock_irqrestore(&dev->rx_lock, flags);

    mutex_unlock(&dev->state_lock);

    wake_up_interruptible(&dev->read_wq);
}

static int sl_uart_write_line(struct smartlight_dev *dev, const char *buf, size_t len)
{
    char kbuf[SMARTLIGHT_MAX_FRAME];
    loff_t pos = 0;
    size_t n;
    ssize_t written;

    if (!dev->uart_filp)
        return -ENODEV;
    if (!buf || !len)
        return -EINVAL;

    n = min(len, (size_t)(SMARTLIGHT_MAX_FRAME - 2));
    memcpy(kbuf, buf, n);
    if (kbuf[n - 1] != '\n')
        kbuf[n++] = '\n';
    kbuf[n] = '\0';

    written = kernel_write(dev->uart_filp, kbuf, n, &pos);
    if (written < 0)
        return (int)written;
    if (written != n)
        return -EIO;

    mutex_lock(&dev->state_lock);
    sl_cache_frame(&dev->last_tx_frame, kbuf, n);
    dev->stats.tx_frames++;
    mutex_unlock(&dev->state_lock);

    return 0;
}

static int sl_rx_thread_fn(void *arg)
{
    struct smartlight_dev *dev = arg;
    char line[SMARTLIGHT_MAX_FRAME];
    loff_t pos = 0;
    size_t idx = 0;
    bool discard_until_newline = false;

    while (!kthread_should_stop()) {
        char ch;
        ssize_t ret;

        ret = kernel_read(dev->uart_filp, &ch, 1, &pos);
        if (ret == -EAGAIN || ret == 0) {
            msleep(SMARTLIGHT_READ_SLEEP_MS);
            continue;
        }

        if (ret < 0) {
            mutex_lock(&dev->state_lock);
            dev->stats.uart_read_errors++;
            mutex_unlock(&dev->state_lock);
            msleep(SMARTLIGHT_READ_SLEEP_MS);
            continue;
        }

        if (ch == '\r')
            continue;

        if (discard_until_newline) {
            if (ch == '\n') {
                discard_until_newline = false;
                idx = 0;
            }
            continue;
        }

        if (ch == '\n') {
            if (idx > 0) {
                line[idx] = '\0';
                sl_handle_rx_line(dev, line, idx);
                idx = 0;
            }
            continue;
        }

        if (idx >= SMARTLIGHT_MAX_FRAME - 1) {
            mutex_lock(&dev->state_lock);
            dev->stats.dropped_frames++;
            mutex_unlock(&dev->state_lock);
            idx = 0;
            discard_until_newline = true;
            continue;
        }

        line[idx++] = ch;
    }

    return 0;
}

static int smartlight_open(struct inode *inode, struct file *file)
{
    struct smartlight_dev *dev = container_of(inode->i_cdev, struct smartlight_dev, cdev);

    file->private_data = dev;

    mutex_lock(&dev->state_lock);
    dev->stats.open_count++;
    mutex_unlock(&dev->state_lock);

    return 0;
}

static int smartlight_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t smartlight_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
    struct smartlight_dev *dev = file->private_data;
    smartlight_frame_t frame;
    unsigned long flags;
    int ret;
    char outbuf[SMARTLIGHT_MAX_FRAME + 2];
    size_t outlen;

    if (!dev)
        return -ENODEV;
    if (!len)
        return 0;

    if (file->f_flags & O_NONBLOCK) {
        if (!READ_ONCE(dev->rx_count))
            return -EAGAIN;
    } else {
        ret = wait_event_interruptible(dev->read_wq,
                                       READ_ONCE(dev->rx_count) > 0 ||
                                       READ_ONCE(dev->shutting_down));
        if (ret)
            return ret;
    }

    spin_lock_irqsave(&dev->rx_lock, flags);
    if (!dev->rx_count) {
        spin_unlock_irqrestore(&dev->rx_lock, flags);
        return READ_ONCE(dev->shutting_down) ? 0 : -EAGAIN;
    }

    memcpy(&frame, &dev->rx_queue[dev->rx_tail], sizeof(frame));
    dev->rx_tail = (dev->rx_tail + 1) % SMARTLIGHT_QUEUE_DEPTH;
    dev->rx_count--;
    spin_unlock_irqrestore(&dev->rx_lock, flags);

    outlen = min((size_t)frame.length, (size_t)SMARTLIGHT_MAX_FRAME);
    memcpy(outbuf, frame.data, outlen);

    if (outlen == 0 || outbuf[outlen - 1] != '\n')
        outbuf[outlen++] = '\n';

    if (len < outlen)
        outlen = len;

    if (copy_to_user(buf, outbuf, outlen))
        return -EFAULT;

    return outlen;
}

static ssize_t smartlight_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
    struct smartlight_dev *dev = file->private_data;
    char kbuf[SMARTLIGHT_MAX_FRAME];
    size_t n;
    int ret;

    if (!dev)
        return -ENODEV;
    if (!len)
        return 0;

    n = min(len, (size_t)(SMARTLIGHT_MAX_FRAME - 2));
    if (copy_from_user(kbuf, buf, n))
        return -EFAULT;

    if (n > 0 && kbuf[n - 1] == '\0')
        n--;

    ret = sl_uart_write_line(dev, kbuf, n);
    if (ret) {
        mutex_lock(&dev->state_lock);
        dev->stats.uart_write_errors++;
        mutex_unlock(&dev->state_lock);
        return ret;
    }

    return len;
}

static __poll_t smartlight_poll(struct file *file, poll_table *wait)
{
    struct smartlight_dev *dev = file->private_data;
    __poll_t mask = POLLOUT | POLLWRNORM;

    if (!dev)
        return POLLERR;

    poll_wait(file, &dev->read_wq, wait);

    if (READ_ONCE(dev->rx_count) > 0)
        mask |= POLLIN | POLLRDNORM;
    if (READ_ONCE(dev->shutting_down))
        mask |= POLLHUP;

    return mask;
}

static long smartlight_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct smartlight_dev *dev = file->private_data;
    long ret = 0;

    if (!dev)
        return -ENODEV;

    mutex_lock(&dev->state_lock);

    switch (cmd) {
    case SL_IOC_GET_LAST_STATUS: {
        smartlight_status_v3_t compat;
        sl_fill_status_v3_from_v4(&dev->last_status, &compat);
        if (copy_to_user((void __user *)arg, &compat, sizeof(compat)))
            ret = -EFAULT;
        break;
    }
    case SL_IOC_GET_LAST_STATUS_V5:
        if (copy_to_user((void __user *)arg, &dev->last_status, sizeof(dev->last_status)))
            ret = -EFAULT;
        break;
    case SL_IOC_GET_LAST_STATUS_LEGACY: {
        struct smartlight_status_v3_legacy legacy;
        sl_fill_legacy_status(&dev->last_status, &legacy);
        if (copy_to_user((void __user *)arg, &legacy, sizeof(legacy)))
            ret = -EFAULT;
        break;
    }
    case SL_IOC_GET_LAST_RX_FRAME:
        if (copy_to_user((void __user *)arg, &dev->last_rx_frame, sizeof(dev->last_rx_frame)))
            ret = -EFAULT;
        break;
    case SL_IOC_GET_LAST_TX_FRAME:
        if (copy_to_user((void __user *)arg, &dev->last_tx_frame, sizeof(dev->last_tx_frame)))
            ret = -EFAULT;
        break;
    case SL_IOC_CLEAR_BUFFERS:
    case SL_IOC_CLEAR_BUFFERS_LEGACY: {
        unsigned long flags;
        memset(&dev->last_rx_frame, 0, sizeof(dev->last_rx_frame));
        memset(&dev->last_tx_frame, 0, sizeof(dev->last_tx_frame));
        sl_status_set_defaults_v4(&dev->last_status);
        spin_lock_irqsave(&dev->rx_lock, flags);
        memset(dev->rx_queue, 0, sizeof(dev->rx_queue));
        dev->rx_head = 0;
        dev->rx_tail = 0;
        dev->rx_count = 0;
        spin_unlock_irqrestore(&dev->rx_lock, flags);
        break;
    }
    case SL_IOC_GET_STATS:
        if (copy_to_user((void __user *)arg, &dev->stats, sizeof(dev->stats)))
            ret = -EFAULT;
        break;
    default:
        ret = -ENOTTY;
        break;
    }

    mutex_unlock(&dev->state_lock);
    return ret;
}

static const struct file_operations smartlight_fops = {
    .owner = THIS_MODULE,
    .open = smartlight_open,
    .release = smartlight_release,
    .read = smartlight_read,
    .write = smartlight_write,
    .poll = smartlight_poll,
    .unlocked_ioctl = smartlight_ioctl,
    .llseek = noop_llseek,
};

static int sl_open_uart(struct smartlight_dev *dev)
{
    dev->uart_filp = filp_open(uart_path, O_RDWR | O_NOCTTY | O_NONBLOCK, 0);
    if (IS_ERR(dev->uart_filp)) {
        int err = PTR_ERR(dev->uart_filp);
        dev->uart_filp = NULL;
        pr_err(DRIVER_TAG ": failed to open UART %s (%d)\n", uart_path, err);
        return err;
    }

    pr_info(DRIVER_TAG ": UART bridge opened on %s\n", uart_path);
    return 0;
}

static void sl_close_uart(struct smartlight_dev *dev)
{
    if (dev->uart_filp) {
        filp_close(dev->uart_filp, NULL);
        dev->uart_filp = NULL;
    }
}

static int __init smartlight_init(void)
{
    int ret;
    struct smartlight_dev *dev = &g_dev;

    memset(dev, 0, sizeof(*dev));
    mutex_init(&dev->state_lock);
    spin_lock_init(&dev->rx_lock);
    init_waitqueue_head(&dev->read_wq);
    sl_status_set_defaults_v4(&dev->last_status);

    ret = alloc_chrdev_region(&dev->devno, 0, 1, DEVICE_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&dev->cdev, &smartlight_fops);
    dev->cdev.owner = THIS_MODULE;

    ret = cdev_add(&dev->cdev, dev->devno, 1);
    if (ret)
        goto err_unregister;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    dev->class = class_create(CLASS_NAME);
#else
    dev->class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(dev->class)) {
        ret = PTR_ERR(dev->class);
        goto err_cdev_del;
    }

    dev->device = device_create(dev->class, NULL, dev->devno, NULL, DEVICE_NAME);
    if (IS_ERR(dev->device)) {
        ret = PTR_ERR(dev->device);
        goto err_class_destroy;
    }

    ret = sl_open_uart(dev);
    if (ret)
        goto err_device_destroy;

    dev->rx_thread = kthread_run(sl_rx_thread_fn, dev, "smartlight_rx");
    if (IS_ERR(dev->rx_thread)) {
        ret = PTR_ERR(dev->rx_thread);
        dev->rx_thread = NULL;
        goto err_uart_close;
    }

    pr_info(DRIVER_TAG ": loaded, /dev/%s ready\n", DEVICE_NAME);
    return 0;

err_uart_close:
    sl_close_uart(dev);
err_device_destroy:
    device_destroy(dev->class, dev->devno);
err_class_destroy:
    class_destroy(dev->class);
err_cdev_del:
    cdev_del(&dev->cdev);
err_unregister:
    unregister_chrdev_region(dev->devno, 1);
    return ret;
}

static void __exit smartlight_exit(void)
{
    struct smartlight_dev *dev = &g_dev;

    WRITE_ONCE(dev->shutting_down, true);
    wake_up_interruptible(&dev->read_wq);

    if (dev->rx_thread)
        kthread_stop(dev->rx_thread);
    sl_close_uart(dev);

    if (dev->device)
        device_destroy(dev->class, dev->devno);
    if (dev->class)
        class_destroy(dev->class);

    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devno, 1);

    pr_info(DRIVER_TAG ": unloaded\n");
}

module_init(smartlight_init);
module_exit(smartlight_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SmartLight Project");
MODULE_DESCRIPTION("SmartLight final project char driver with tolerant parser, debug logs, and raw UART Protocol V3 bridge");
