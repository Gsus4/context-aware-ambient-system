#ifndef SLGW_CONFIG_H
#define SLGW_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define SLGW_STR_SMALL 64
#define SLGW_STR_MID 128
#define SLGW_STR_LARGE 256
#define SLGW_PATH_SIZE 256
#define SLGW_TOPIC_SIZE 256
#define SLGW_JSON_SIZE 2048
#define SLGW_LINE_SIZE 512
#define SLGW_COMMAND_QUEUE_SIZE 64
#define SLGW_OFFLINE_QUEUE_CAPACITY 256
#define SLGW_ARGS_MAX 8
#define SLGW_ARG_TEXT_SIZE 64

#ifdef __cplusplus
#error "Do not compile this gateway with C++"
#endif

typedef struct {
    char transport_mode[SLGW_STR_SMALL];
    char node_id[SLGW_STR_SMALL];

    char mqtt_host[SLGW_STR_MID];
    int mqtt_port;
    int mqtt_keepalive;
    char mqtt_username[SLGW_STR_MID];
    char mqtt_password[SLGW_STR_LARGE];
    char mqtt_password_file[SLGW_PATH_SIZE];
    char mqtt_client_id[SLGW_STR_MID];
    bool mqtt_clean_session;
    int mqtt_connect_timeout_ms;
    int mqtt_reconnect_min_delay_s;
    int mqtt_reconnect_max_delay_s;

    char mqtt_internal_topic_base[SLGW_STR_MID];
    bool integration_enabled;
    char integration_topic_base[SLGW_STR_MID];

    int mqtt_command_subscribe_qos;
    int mqtt_ack_qos;
    int mqtt_status_qos;
    int mqtt_event_qos;

    int offline_queue_max;
    int command_queue_max;
    int command_timeout_ms;
    int flow_gradient_timeout_ms;
    int status_publish_interval_ms;
    int status_poll_timeout_ms;
    int status_sync_cooldown_ms;
    int transport_reconnect_delay_ms;
    int startup_status_timeout_ms;
    bool startup_status_sync;

    char driver_device[SLGW_PATH_SIZE];
    char serial_port[SLGW_PATH_SIZE];
    int serial_baudrate;
    int serial_timeout_ms;

    char process_lock_file[SLGW_PATH_SIZE];
    bool debug;
    int run_seconds;

    char load_error[SLGW_STR_LARGE];
} slgw_config;

int slgw_config_load(slgw_config *cfg);
void slgw_config_print(const slgw_config *cfg);
int slgw_config_read_password_file(const char *path, char *out, size_t out_size);

#endif
