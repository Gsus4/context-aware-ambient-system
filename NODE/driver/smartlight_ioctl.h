#ifndef SMARTLIGHT_IOCTL_H
#define SMARTLIGHT_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define SMARTLIGHT_IOCTL_MAGIC 's'

#define SMARTLIGHT_MAX_FRAME   256
#define SMARTLIGHT_MODE_LEN    16
#define SMARTLIGHT_SCENE_LEN   16
#define SMARTLIGHT_STATE_LEN   16
#define SMARTLIGHT_PRESET_LEN  16
#define SMARTLIGHT_SPEED_LEN   16
#define SMARTLIGHT_STRENGTH_LEN 16

typedef struct smartlight_status_v3 {
    __u32 session_id;
    __u32 seq;
    __u32 uptime_ms;
    __u32 lux_x100;

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
} smartlight_status_v3_t;

typedef struct smartlight_status_v5 {
    __u32 uptime_ms;
    __s32 brightness_pct;
    __s32 tone_bias;
    __s32 flow_brightness_pct;

    __u8 scene_modified;
    __u8 flow_enabled;
    __u8 flow_soft_mode;
    __u8 breathing_enabled;
    __u8 manual_override;

    char active_mode[SMARTLIGHT_MODE_LEN];
    char active_scene[SMARTLIGHT_SCENE_LEN];
    char flow_preset[SMARTLIGHT_PRESET_LEN];
    char flow_speed[SMARTLIGHT_SPEED_LEN];
    char breathing_speed[SMARTLIGHT_SPEED_LEN];
    char breathing_strength[SMARTLIGHT_STRENGTH_LEN];
} smartlight_status_v5_t;
typedef smartlight_status_v5_t smartlight_status_v4_t;

typedef struct smartlight_frame {
    __u32 length;
    char data[SMARTLIGHT_MAX_FRAME];
} smartlight_frame_t;

typedef struct smartlight_stats {
    __u32 open_count;
    __u32 tx_frames;
    __u32 rx_frames;
    __u32 dropped_frames;
    __u32 parse_errors;
    __u32 uart_write_errors;
    __u32 uart_read_errors;
} smartlight_stats_t;

#define SL_IOC_GET_LAST_STATUS      _IOR(SMARTLIGHT_IOCTL_MAGIC, 0x01, smartlight_status_v3_t)
#define SL_IOC_GET_LAST_RX_FRAME    _IOR(SMARTLIGHT_IOCTL_MAGIC, 0x02, smartlight_frame_t)
#define SL_IOC_GET_LAST_TX_FRAME    _IOR(SMARTLIGHT_IOCTL_MAGIC, 0x03, smartlight_frame_t)
#define SL_IOC_CLEAR_BUFFERS        _IO(SMARTLIGHT_IOCTL_MAGIC,  0x04)
#define SL_IOC_GET_STATS            _IOR(SMARTLIGHT_IOCTL_MAGIC, 0x05, smartlight_stats_t)
#define SL_IOC_GET_LAST_STATUS_V5   _IOR(SMARTLIGHT_IOCTL_MAGIC, 0x06, smartlight_status_v5_t)
#define SL_IOC_GET_LAST_STATUS_V4   SL_IOC_GET_LAST_STATUS_V5

#endif
