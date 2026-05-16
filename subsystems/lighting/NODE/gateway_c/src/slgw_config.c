#include "slgw_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "slgw_text.h"

static const char *slgw_env_or(const char *name, const char *fallback) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }
    return value;
}

static bool slgw_env_bool(const char *name, bool fallback) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }
    if (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0) {
        return true;
    }
    if (strcmp(value, "0") == 0 || strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0) {
        return false;
    }
    return fallback;
}

static int slgw_copy_env(char *dst, size_t dst_size, const char *name, const char *fallback, char *error, size_t error_size) {
    const char *value = slgw_env_or(name, fallback);
    if (slgw_copy_text_trimmed(dst, dst_size, value) != 0) {
        slgw_format(error, error_size, "config field too long: %s", name);
        return -1;
    }
    return 0;
}

static int slgw_int_env(const char *name, const char *fallback, int *out_value, char *error, size_t error_size) {
    if (slgw_parse_int(slgw_env_or(name, fallback), out_value) != 0) {
        slgw_format(error, error_size, "config integer invalid: %s", name);
        return -1;
    }
    return 0;
}

int slgw_config_read_password_file(const char *path, char *out, size_t out_size) {
    FILE *fp;
    size_t nread;
    if (path == NULL || path[0] == '\0' || out == NULL || out_size == 0) {
        return 0;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    nread = fread(out, 1, out_size - 1, fp);
    fclose(fp);
    out[nread] = '\0';
    while (nread > 0) {
        char ch = out[nread - 1];
        if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t') {
            out[nread - 1] = '\0';
            nread--;
            continue;
        }
        break;
    }
    return 0;
}

int slgw_config_load(slgw_config *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    if (slgw_copy_env(cfg->transport_mode, sizeof(cfg->transport_mode), "TRANSPORT_MODE", "driver", cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_copy_env(cfg->node_id, sizeof(cfg->node_id), "NODE_ID", "bedroom01", cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_copy_env(cfg->mqtt_host, sizeof(cfg->mqtt_host), "MQTT_BROKER_HOST", "127.0.0.1", cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("MQTT_BROKER_PORT", "1883", &cfg->mqtt_port, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("MQTT_KEEPALIVE", "30", &cfg->mqtt_keepalive, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_copy_env(cfg->mqtt_username, sizeof(cfg->mqtt_username), "MQTT_USERNAME", "", cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_copy_env(cfg->mqtt_password, sizeof(cfg->mqtt_password), "MQTT_PASSWORD", "", cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_copy_env(cfg->mqtt_password_file, sizeof(cfg->mqtt_password_file), "MQTT_PASSWORD_FILE", "", cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_copy_env(cfg->mqtt_client_id, sizeof(cfg->mqtt_client_id), "MQTT_CLIENT_ID", "", cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    cfg->mqtt_clean_session = slgw_env_bool("MQTT_CLEAN_SESSION", false);
    if (slgw_int_env("MQTT_CONNECT_TIMEOUT_MS", "10000", &cfg->mqtt_connect_timeout_ms, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("MQTT_RECONNECT_MIN_DELAY", "1", &cfg->mqtt_reconnect_min_delay_s, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("MQTT_RECONNECT_MAX_DELAY", "30", &cfg->mqtt_reconnect_max_delay_s, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;

    if (slgw_copy_env(cfg->mqtt_internal_topic_base, sizeof(cfg->mqtt_internal_topic_base), "MQTT_INTERNAL_TOPIC_BASE", "smartlight", cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    cfg->integration_enabled = slgw_env_bool("INTEGRATION_ENABLED", false);
    if (slgw_copy_env(cfg->integration_topic_base, sizeof(cfg->integration_topic_base), "INTEGRATION_TOPIC_BASE", "smartlight", cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;

    if (slgw_int_env("MQTT_COMMAND_SUBSCRIBE_QOS", "1", &cfg->mqtt_command_subscribe_qos, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("MQTT_ACK_QOS", "1", &cfg->mqtt_ack_qos, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("MQTT_STATUS_QOS", "0", &cfg->mqtt_status_qos, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("MQTT_EVENT_QOS", "0", &cfg->mqtt_event_qos, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;

    if (slgw_int_env("OFFLINE_QUEUE_MAX", "200", &cfg->offline_queue_max, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (cfg->offline_queue_max <= 0 || cfg->offline_queue_max > SLGW_OFFLINE_QUEUE_CAPACITY) {
        cfg->offline_queue_max = SLGW_OFFLINE_QUEUE_CAPACITY;
    }
    if (slgw_int_env("COMMAND_QUEUE_MAX", "64", &cfg->command_queue_max, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (cfg->command_queue_max <= 0 || cfg->command_queue_max > SLGW_COMMAND_QUEUE_SIZE) {
        cfg->command_queue_max = SLGW_COMMAND_QUEUE_SIZE;
    }
    if (slgw_int_env("COMMAND_TIMEOUT_MS", "2000", &cfg->command_timeout_ms, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("FLOW_GRADIENT_TIMEOUT_MS", "6000", &cfg->flow_gradient_timeout_ms, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("STATUS_PUBLISH_INTERVAL_MS", "5000", &cfg->status_publish_interval_ms, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("STATUS_POLL_TIMEOUT_MS", "2000", &cfg->status_poll_timeout_ms, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("STATUS_SYNC_COOLDOWN_MS", "600", &cfg->status_sync_cooldown_ms, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("TRANSPORT_RECONNECT_DELAY_MS", "1000", &cfg->transport_reconnect_delay_ms, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    cfg->startup_status_sync = slgw_env_bool("STARTUP_STATUS_SYNC", true);
    if (slgw_int_env("STARTUP_STATUS_TIMEOUT_MS", "2000", &cfg->startup_status_timeout_ms, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;

    if (slgw_copy_env(cfg->driver_device, sizeof(cfg->driver_device), "DRIVER_DEVICE", "/dev/smartlight0", cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_copy_env(cfg->serial_port, sizeof(cfg->serial_port), "SERIAL_PORT", "/dev/serial0", cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("SERIAL_BAUDRATE", "115200", &cfg->serial_baudrate, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    if (slgw_int_env("SERIAL_TIMEOUT_MS", "200", &cfg->serial_timeout_ms, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;

    if (slgw_copy_env(cfg->process_lock_file, sizeof(cfg->process_lock_file), "PROCESS_LOCK_FILE", "", cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;
    cfg->debug = slgw_env_bool("DEBUG", false);
    if (slgw_int_env("SLGW_RUN_SECONDS", "0", &cfg->run_seconds, cfg->load_error, sizeof(cfg->load_error)) != 0) return -1;

    if (cfg->mqtt_client_id[0] == '\0') {
        if (slgw_format(cfg->mqtt_client_id, sizeof(cfg->mqtt_client_id), "smartlight-gateway-%s-%s-c", cfg->node_id, cfg->transport_mode) != 0) {
            slgw_copy_text(cfg->load_error, sizeof(cfg->load_error), "mqtt_client_id too long");
            return -1;
        }
    }
    if (cfg->process_lock_file[0] == '\0') {
        if (slgw_format(cfg->process_lock_file, sizeof(cfg->process_lock_file), "/tmp/smartlight-gateway-%s.lock", cfg->node_id) != 0) {
            slgw_copy_text(cfg->load_error, sizeof(cfg->load_error), "process_lock_file too long");
            return -1;
        }
    }
    if (cfg->mqtt_password[0] == '\0' && cfg->mqtt_password_file[0] != '\0') {
        if (slgw_config_read_password_file(cfg->mqtt_password_file, cfg->mqtt_password, sizeof(cfg->mqtt_password)) != 0) {
            slgw_format(cfg->load_error, sizeof(cfg->load_error), "cannot read password file: %s", cfg->mqtt_password_file);
            return -1;
        }
    }
    return 0;
}

void slgw_config_print(const slgw_config *cfg) {
    printf("[SLGW][CONFIG] transport=%s node_id=%s mqtt=%s:%d internal_base=%s integration=%s driver=%s serial=%s\n",
           cfg->transport_mode,
           cfg->node_id,
           cfg->mqtt_host,
           cfg->mqtt_port,
           cfg->mqtt_internal_topic_base,
           cfg->integration_enabled ? "true" : "false",
           cfg->driver_device,
           cfg->serial_port);
}
