#define _POSIX_C_SOURCE 200809L
#include "slgw_app.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "slgw_text.h"
#include "slgw_topics.h"

static volatile sig_atomic_t slgw_signal_stop = 0;

#define SLGW_SCENE_COALESCE_DELAY_MS 150

static unsigned long long slgw_now_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((unsigned long long)ts.tv_sec * 1000ULL) + ((unsigned long long)ts.tv_nsec / 1000000ULL);
}

static void slgw_sleep_ms(int delay_ms) {
    struct timespec ts;
    if (delay_ms <= 0) {
        return;
    }
    ts.tv_sec = delay_ms / 1000;
    ts.tv_nsec = (long)(delay_ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void slgw_make_realtime_deadline_ms(struct timespec *ts, int delay_ms) {
    if (ts == NULL) {
        return;
    }
    if (delay_ms < 0) {
        delay_ms = 0;
    }
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += delay_ms / 1000;
    ts->tv_nsec += (long)(delay_ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

static void slgw_handle_signal(int signum) {
    (void)signum;
    slgw_signal_stop = 1;
}

static int slgw_next_req_id_text(slgw_app *app, char *out, size_t out_size) {
    unsigned int seq;
    if (out == NULL || out_size == 0) {
        return -1;
    }
    pthread_mutex_lock(&app->req_id_mutex);
    seq = app->next_req_seq++;
    if (app->next_req_seq == 0 || app->next_req_seq > 2000000000U) {
        app->next_req_seq = 1;
        app->pending_status_req_id[0] = '\0';
        app->pending_status_has_req_id = false;
    }
    pthread_mutex_unlock(&app->req_id_mutex);
    return slgw_format(out, out_size, "node-%u", seq);
}

static int slgw_publish_text_direct(struct mosquitto *client, const char *topic, const char *payload, int qos, bool retain) {
    int rc;
    if (client == NULL || topic == NULL || payload == NULL) {
        return MOSQ_ERR_INVAL;
    }
    rc = mosquitto_publish(client, NULL, topic, (int)strlen(payload), payload, qos, retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[SLGW][MQTT][PUBLISH_FAIL] topic=%s rc=%d\n", topic, rc);
    }
    return rc;
}

static int slgw_publish_text_main(slgw_app *app, const char *topic, const char *payload, int qos, bool retain) {
    int rc;
    pthread_mutex_lock(&app->publish_mutex);
    rc = slgw_publish_text_direct(app->mqtt_main, topic, payload, qos, retain);
    pthread_mutex_unlock(&app->publish_mutex);
    return rc;
}

static void slgw_queue_offline_publish(slgw_app *app, const char *topic, const char *payload, int qos, bool retain) {
    pthread_mutex_lock(&app->offline_queue_mutex);
    if (slgw_publish_buffer_enqueue(&app->offline_queue, topic, payload, qos, retain) != 0) {
        fprintf(stderr, "[SLGW][OFFLINE_QUEUE] enqueue failed topic=%s\n", topic);
    }
    pthread_mutex_unlock(&app->offline_queue_mutex);
}

static void slgw_flush_offline_queue(slgw_app *app) {
    slgw_publish_item item;
    for (;;) {
        int rc;
        pthread_mutex_lock(&app->offline_queue_mutex);
        rc = slgw_publish_buffer_pop(&app->offline_queue, &item);
        pthread_mutex_unlock(&app->offline_queue_mutex);
        if (rc <= 0) {
            break;
        }
        if (slgw_publish_text_main(app, item.topic, item.payload, item.qos, item.retain) != MOSQ_ERR_SUCCESS) {
            slgw_queue_offline_publish(app, item.topic, item.payload, item.qos, item.retain);
            break;
        }
    }
}

static void slgw_publish_or_queue(slgw_app *app, const char *topic, const char *payload, int qos, bool retain) {
    if (!app->mqtt_main_connected) {
        slgw_queue_offline_publish(app, topic, payload, qos, retain);
        return;
    }
    if (slgw_publish_text_main(app, topic, payload, qos, retain) != MOSQ_ERR_SUCCESS) {
        slgw_queue_offline_publish(app, topic, payload, qos, retain);
    }
}

static void slgw_publish_availability_pair(slgw_app *app, bool online, const char *reason) {
    char topic[SLGW_TOPIC_SIZE];
    const char *payload = online ? "online" : "offline";
    (void)reason;
    if (slgw_topic_mcu_availability(topic, sizeof(topic), &app->cfg) == 0) {
        slgw_publish_or_queue(app, topic, payload, 1, true);
    }
}

static void slgw_publish_ack_for_request(slgw_app *app, const slgw_command_request *request, bool ok, const char *message, const slgw_resp_frame *resp, const char *error_type) {
    char topic[SLGW_TOPIC_SIZE];
    char payload[SLGW_JSON_SIZE];
    if (slgw_json_build_ack(payload, sizeof(payload), &app->cfg, request, ok, message, resp, error_type) == 0 &&
        slgw_topic_mcu_ack(topic, sizeof(topic), &app->cfg) == 0) {
        slgw_publish_or_queue(app, topic, payload, app->cfg.mqtt_ack_qos, false);
    }
}

static void slgw_publish_status_pair(slgw_app *app, const slgw_status_frame *status, const char *req_id) {
    char topic[SLGW_TOPIC_SIZE];
    char payload[SLGW_JSON_SIZE];
    if (slgw_json_build_status(payload, sizeof(payload), &app->cfg, status, req_id) == 0 &&
        slgw_topic_mcu_status(topic, sizeof(topic), &app->cfg) == 0) {
        slgw_publish_or_queue(app, topic, payload, app->cfg.mqtt_status_qos, true);
    }
}

static void slgw_publish_event_pair(slgw_app *app, const slgw_event_frame *event_frame) {
    char topic[SLGW_TOPIC_SIZE];
    char payload[SLGW_JSON_SIZE];
    if (slgw_json_build_event(payload, sizeof(payload), &app->cfg, event_frame) == 0 &&
        slgw_topic_mcu_event(topic, sizeof(topic), &app->cfg) == 0) {
        slgw_publish_or_queue(app, topic, payload, app->cfg.mqtt_event_qos, false);
    }
}

static void slgw_pending_prepare(slgw_app *app, const char *req_id) {
    pthread_mutex_lock(&app->pending.mutex);
    memset(&app->pending.response, 0, sizeof(app->pending.response));
    app->pending.active = true;
    app->pending.ready = false;
    slgw_copy_text(app->pending.req_id, sizeof(app->pending.req_id), req_id == NULL ? "" : req_id);
    pthread_mutex_unlock(&app->pending.mutex);
}

static void slgw_pending_clear(slgw_app *app) {
    pthread_mutex_lock(&app->pending.mutex);
    app->pending.active = false;
    app->pending.ready = false;
    app->pending.req_id[0] = '\0';
    pthread_mutex_unlock(&app->pending.mutex);
}

static int slgw_wait_for_response(slgw_app *app, int timeout_ms, slgw_resp_frame *out) {
    struct timespec ts;
    int rc = 0;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&app->pending.mutex);
    while (!app->pending.ready && rc == 0) {
        rc = pthread_cond_timedwait(&app->pending.cond, &app->pending.mutex, &ts);
    }
    if (rc == 0 && app->pending.ready) {
        memcpy(out, &app->pending.response, sizeof(*out));
        app->pending.active = false;
        app->pending.ready = false;
        app->pending.req_id[0] = '\0';
        pthread_mutex_unlock(&app->pending.mutex);
        return 0;
    }
    app->pending.active = false;
    app->pending.ready = false;
    app->pending.req_id[0] = '\0';
    pthread_mutex_unlock(&app->pending.mutex);
    return rc == ETIMEDOUT ? -1 : -2;
}

static int slgw_send_protocol_command(slgw_app *app, const slgw_command_request *request, slgw_resp_frame *resp) {
    char line[SLGW_LINE_SIZE];
    char arg_text[SLGW_LINE_SIZE];
    char req_id[64];
    const char *command_name = NULL;
    int timeout_ms = request->context.timeout_override_ms > 0 ? request->context.timeout_override_ms : app->cfg.command_timeout_ms;
    int rc = 0;

    memset(arg_text, 0, sizeof(arg_text));
    switch (request->type) {
        case SLGW_CMD_GET_STATUS:
            command_name = "GET_STATUS";
            break;
        case SLGW_CMD_SET_SCENE:
            command_name = "SET_SCENE";
            if (request->scene_brightness_set || request->tone_bias_set || request->scene_color_temp_k_set) {
                rc = slgw_format(arg_text, sizeof(arg_text), "%s,%d,%d,%d", request->scene,
                                 request->scene_brightness_set ? request->scene_brightness_pct : -1,
                                 request->tone_bias_set ? request->tone_bias : 0,
                                 request->scene_color_temp_k_set ? request->scene_color_temp_k : -1);
            } else {
                rc = slgw_copy_upper_text(arg_text, sizeof(arg_text), request->scene);
            }
            break;
        case SLGW_CMD_RESTORE_SCENE:
            command_name = "RESTORE_SCENE";
            break;
        case SLGW_CMD_SET_STATIC:
            command_name = "SET_STATIC";
            if (request->static_color_temp_k_set) {
                rc = slgw_format(arg_text, sizeof(arg_text), "%d,%d,%d,%d,%d", request->static_brightness_pct, -1, -1, -1, request->static_color_temp_k);
            } else {
                rc = slgw_format(arg_text, sizeof(arg_text), "%d,%d,%d,%d", request->static_brightness_pct, request->static_r, request->static_g, request->static_b);
            }
            break;
        case SLGW_CMD_SET_STATIC_BREATHING:
            command_name = "SET_BREATHING";
            rc = slgw_format(arg_text, sizeof(arg_text), "%s,%s,%s", request->breathing_enabled ? "1" : "0", request->breathing_speed, request->breathing_strength);
            break;
        case SLGW_CMD_SET_CUSTOM:
            command_name = "SET_CUSTOM";
            if (strcmp(request->custom_control_type, "fixed_brightness") == 0) {
                if (strcmp(request->color_mode, "rgb") == 0) {
                    rc = slgw_format(arg_text, sizeof(arg_text), "fixed_brightness,%d,rgb,%d,%d,%d",
                                     request->fixed_brightness_pct,
                                     request->custom_r,
                                     request->custom_g,
                                     request->custom_b);
                } else {
                    rc = slgw_format(arg_text, sizeof(arg_text), "fixed_brightness,%d,cct,%d",
                                     request->fixed_brightness_pct,
                                     request->custom_color_temp_k);
                }
            } else {
                if (strcmp(request->color_mode, "rgb") == 0) {
                    rc = slgw_format(arg_text, sizeof(arg_text), "target_lux_range,%d,%d,%d,%d,rgb,%d,%d,%d",
                                     request->target_lux,
                                     request->tolerance_lux,
                                     request->min_output,
                                     request->max_output,
                                     request->custom_r,
                                     request->custom_g,
                                     request->custom_b);
                } else {
                    rc = slgw_format(arg_text, sizeof(arg_text), "target_lux_range,%d,%d,%d,%d,cct,%d",
                                     request->target_lux,
                                     request->tolerance_lux,
                                     request->min_output,
                                     request->max_output,
                                     request->custom_color_temp_k);
                }
            }
            break;
        case SLGW_CMD_SET_AUTO:
            command_name = "SET_AUTO";
            rc = slgw_copy_text(arg_text, sizeof(arg_text), request->auto_request_enabled ? "1" : "0");
            break;
        case SLGW_CMD_SET_FLOW:
            command_name = "SET_FLOW";
            rc = slgw_format(arg_text, sizeof(arg_text), "%s,%s,%d,%s", request->preset, request->flow_speed, request->flow_brightness, request->flow_soft_mode ? "1" : "0");
            break;
        case SLGW_CMD_POWER_OFF:
            command_name = "POWER_OFF";
            break;
        default:
            return -10;
    }
    if (rc != 0) {
        return -11;
    }

    pthread_mutex_lock(&app->protocol_mutex);
    if (request->context.request_id[0] != '\0') {
        if (slgw_copy_text(req_id, sizeof(req_id), request->context.request_id) != 0) {
            pthread_mutex_unlock(&app->protocol_mutex);
            return -12;
        }
    } else if (slgw_next_req_id_text(app, req_id, sizeof(req_id)) != 0) {
        pthread_mutex_unlock(&app->protocol_mutex);
        return -12;
    }
    if (slgw_protocol_build_cmd_line(line, sizeof(line), req_id, command_name, arg_text[0] == '\0' ? NULL : arg_text) != 0) {
        pthread_mutex_unlock(&app->protocol_mutex);
        return -12;
    }
    slgw_pending_prepare(app, req_id);
    if (slgw_transport_write_line(&app->transport, line) != 0) {
        slgw_pending_clear(app);
        pthread_mutex_unlock(&app->protocol_mutex);
        return -2;
    }
    rc = slgw_wait_for_response(app, timeout_ms, resp);
    pthread_mutex_unlock(&app->protocol_mutex);
    return rc == 0 ? 0 : -3;
}

static int slgw_command_queue_push(slgw_app *app, const slgw_command_request *request) {
    if (app->command_queue_count >= app->cfg.command_queue_max) {
        return -1;
    }
    app->command_queue[app->command_queue_tail].occupied = true;
    memcpy(&app->command_queue[app->command_queue_tail].request, request, sizeof(*request));
    app->command_queue_tail = (app->command_queue_tail + 1) % SLGW_COMMAND_QUEUE_SIZE;
    app->command_queue_count += 1;
    return 0;
}

static int slgw_command_queue_pop(slgw_app *app, slgw_command_request *out) {
    if (app->command_queue_count <= 0) {
        return -1;
    }
    memcpy(out, &app->command_queue[app->command_queue_head].request, sizeof(*out));
    app->command_queue[app->command_queue_head].occupied = false;
    app->command_queue_head = (app->command_queue_head + 1) % SLGW_COMMAND_QUEUE_SIZE;
    app->command_queue_count -= 1;
    return 0;
}

static int slgw_command_queue_peek(slgw_app *app, slgw_command_request *out) {
    if (app->command_queue_count <= 0) {
        return -1;
    }
    memcpy(out, &app->command_queue[app->command_queue_head].request, sizeof(*out));
    return 0;
}

static int slgw_command_queue_remove_type(slgw_app *app, slgw_command_type type) {
    slgw_command_request kept[SLGW_COMMAND_QUEUE_SIZE];
    int kept_count = 0;
    int removed_count = 0;
    int count = app->command_queue_count;
    int idx;

    for (idx = 0; idx < count; ++idx) {
        int read_idx = (app->command_queue_head + idx) % SLGW_COMMAND_QUEUE_SIZE;
        slgw_command_request *request = &app->command_queue[read_idx].request;
        if (app->command_queue[read_idx].occupied && request->type == type) {
            removed_count += 1;
            continue;
        }
        if (app->command_queue[read_idx].occupied && kept_count < SLGW_COMMAND_QUEUE_SIZE) {
            memcpy(&kept[kept_count], request, sizeof(kept[kept_count]));
            kept_count += 1;
        }
    }

    memset(app->command_queue, 0, sizeof(app->command_queue));
    app->command_queue_head = 0;
    app->command_queue_tail = 0;
    app->command_queue_count = kept_count;
    for (idx = 0; idx < kept_count; ++idx) {
        app->command_queue[idx].occupied = true;
        memcpy(&app->command_queue[idx].request, &kept[idx], sizeof(app->command_queue[idx].request));
    }
    app->command_queue_tail = kept_count % SLGW_COMMAND_QUEUE_SIZE;
    return removed_count;
}

static int slgw_enqueue_command_with_coalescing(slgw_app *app, const slgw_command_request *request) {
    int rc;
    if (request == NULL) {
        return -1;
    }

    if (request->type == SLGW_CMD_POWER_OFF) {
        int removed = slgw_command_queue_remove_type(app, SLGW_CMD_SET_SCENE);
        app->scene_coalesce_until_ms = 0;
        if (removed > 0 && app->cfg.debug) {
            printf("[SLGW][COALESCE] power_off cleared %d pending scene command(s)\n", removed);
        }
        return slgw_command_queue_push(app, request);
    }

    if (request->type == SLGW_CMD_SET_SCENE) {
        int removed = slgw_command_queue_remove_type(app, SLGW_CMD_SET_SCENE);
        rc = slgw_command_queue_push(app, request);
        if (rc == 0) {
            app->scene_coalesce_until_ms = slgw_now_monotonic_ms() + (unsigned long long)SLGW_SCENE_COALESCE_DELAY_MS;
            if (removed > 0 && app->cfg.debug) {
                printf("[SLGW][COALESCE] replaced %d pending scene command(s) with latest scene=%s\n", removed, request->scene);
            }
        }
        return rc;
    }

    return slgw_command_queue_push(app, request);
}

static int slgw_command_queue_wait_and_pop(slgw_app *app, slgw_command_request *out) {
    pthread_mutex_lock(&app->command_queue_mutex);
    for (;;) {
        slgw_command_request next_request;

        while (app->command_queue_count == 0 && !app->stop_requested) {
            pthread_cond_wait(&app->command_queue_cond, &app->command_queue_mutex);
        }
        if (app->stop_requested) {
            pthread_mutex_unlock(&app->command_queue_mutex);
            return -1;
        }
        memset(&next_request, 0, sizeof(next_request));
        if (slgw_command_queue_peek(app, &next_request) != 0) {
            continue;
        }
        if (next_request.type == SLGW_CMD_SET_SCENE) {
            unsigned long long now_ms = slgw_now_monotonic_ms();
            if (app->scene_coalesce_until_ms > now_ms) {
                struct timespec ts;
                unsigned long long wait_ms_ull = app->scene_coalesce_until_ms - now_ms;
                int wait_ms = wait_ms_ull > 1000ULL ? 1000 : (int)wait_ms_ull;
                if (wait_ms <= 0) {
                    wait_ms = 1;
                }
                slgw_make_realtime_deadline_ms(&ts, wait_ms);
                pthread_cond_timedwait(&app->command_queue_cond, &app->command_queue_mutex, &ts);
                continue;
            }
            app->scene_coalesce_until_ms = 0;
        }
        if (slgw_command_queue_pop(app, out) == 0) {
            pthread_mutex_unlock(&app->command_queue_mutex);
            return 0;
        }
    }
}

static bool slgw_request_needs_status_sync(slgw_command_type type) {
    switch (type) {
        case SLGW_CMD_GET_STATUS:
        case SLGW_CMD_SET_SCENE:
        case SLGW_CMD_RESTORE_SCENE:
        case SLGW_CMD_SET_STATIC:
        case SLGW_CMD_SET_STATIC_BREATHING:
        case SLGW_CMD_SET_CUSTOM:
        case SLGW_CMD_SET_AUTO:
        case SLGW_CMD_SET_FLOW:
        case SLGW_CMD_POWER_OFF:
            return true;
        default:
            return false;
    }
}

static void slgw_request_status_sync(slgw_app *app, const char *reason, const char *req_id) {
    pthread_mutex_lock(&app->status_mutex);
    app->pending_status_sync = true;
    if (reason != NULL) {
        slgw_copy_text(app->pending_status_reason, sizeof(app->pending_status_reason), reason);
    } else {
        app->pending_status_reason[0] = '\0';
    }
    if (req_id != NULL && req_id[0] != '\0') {
        app->pending_status_has_req_id = true;
        slgw_copy_text(app->pending_status_req_id, sizeof(app->pending_status_req_id), req_id);
    } else {
        app->pending_status_has_req_id = false;
        app->pending_status_req_id[0] = '\0';
    }
    pthread_cond_signal(&app->status_cond);
    pthread_mutex_unlock(&app->status_mutex);
}

static void *slgw_command_worker_main(void *arg) {
    slgw_app *app = (slgw_app *)arg;
    while (!app->stop_requested) {
        slgw_command_request request;
        slgw_resp_frame response;
        int rc;
        memset(&request, 0, sizeof(request));
        if (slgw_command_queue_wait_and_pop(app, &request) != 0) {
            break;
        }

        memset(&response, 0, sizeof(response));
        rc = slgw_send_protocol_command(app, &request, &response);
        if (rc == 0) {
            bool uart_ok = (strcmp(response.status, "OK") == 0);
            const char *status_req_id = NULL;
            slgw_publish_ack_for_request(app, &request, uart_ok, uart_ok ? "ok" : "uart_error", &response, uart_ok ? NULL : "uart_error");
            if (uart_ok) {
                if (request.type == SLGW_CMD_GET_STATUS && request.context.request_id[0] != '\0') {
                    status_req_id = request.context.request_id;
                }
                if (slgw_request_needs_status_sync(request.type)) {
                    slgw_request_status_sync(app, request.cmd_name, status_req_id);
                }
            }
        } else if (rc == -3) {
            slgw_publish_ack_for_request(app, &request, false, "timeout", NULL, "timeout");
        } else if (rc == -2) {
            slgw_publish_ack_for_request(app, &request, false, "transport_write_failed", NULL, "transport_write_failed");
        } else {
            slgw_publish_ack_for_request(app, &request, false, "unsupported_command", NULL, "unsupported_command");
        }
    }
    return NULL;
}

static int slgw_issue_status_request(slgw_app *app, int timeout_ms) {
    slgw_command_request request;
    slgw_resp_frame response;
    memset(&request, 0, sizeof(request));
    request.valid = true;
    request.type = SLGW_CMD_GET_STATUS;
    slgw_copy_text(request.cmd_name, sizeof(request.cmd_name), "get_status");
    request.context.request_id[0] = '\0';
    request.context.timeout_override_ms = timeout_ms;
    return slgw_send_protocol_command(app, &request, &response) == 0 ? 0 : -1;
}

static void *slgw_status_worker_main(void *arg) {
    slgw_app *app = (slgw_app *)arg;
    unsigned long long periodic_due_ms = 0;
    while (!app->stop_requested) {
        struct timespec ts;
        bool should_sync = false;
        bool startup_sync = false;
        unsigned long long now_ms = slgw_now_monotonic_ms();

        pthread_mutex_lock(&app->status_mutex);
        if (!app->startup_status_done && app->cfg.startup_status_sync && app->transport_connected) {
            startup_sync = true;
            app->startup_status_done = true;
        }
        if (app->pending_status_sync && now_ms >= app->last_status_request_ms + (unsigned long long)app->cfg.status_sync_cooldown_ms) {
            should_sync = true;
            app->pending_status_sync = false;
        }
        if (periodic_due_ms == 0) {
            periodic_due_ms = now_ms + (unsigned long long)app->cfg.status_publish_interval_ms;
        }
        if (now_ms >= periodic_due_ms) {
            should_sync = true;
            periodic_due_ms = now_ms + (unsigned long long)app->cfg.status_publish_interval_ms;
        }
        if (!should_sync && !startup_sync) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 200000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&app->status_cond, &app->status_mutex, &ts);
        }
        pthread_mutex_unlock(&app->status_mutex);

        if (app->stop_requested) {
            break;
        }
        if (!app->transport_connected) {
            continue;
        }
        if (startup_sync) {
            app->last_status_request_ms = slgw_now_monotonic_ms();
            slgw_issue_status_request(app, app->cfg.startup_status_timeout_ms);
            continue;
        }
        if (should_sync) {
            app->last_status_request_ms = slgw_now_monotonic_ms();
            slgw_issue_status_request(app, app->cfg.status_poll_timeout_ms);
        }
    }
    return NULL;
}

static void slgw_mqtt_on_main_connect(struct mosquitto *mosq, void *userdata, int rc) {
    slgw_app *app = (slgw_app *)userdata;
    char topic[SLGW_TOPIC_SIZE];
    (void)mosq;
    app->mqtt_main_connected = (rc == 0);
    printf("[SLGW][MQTT] main connected rc=%d\n", rc);
    if (rc != 0) {
        return;
    }
    if (slgw_topic_server_set(topic, sizeof(topic), &app->cfg) == 0) {
        mosquitto_subscribe(app->mqtt_main, NULL, topic, app->cfg.mqtt_command_subscribe_qos);
    }
    if (app->cfg.integration_enabled && slgw_topic_integration_command(topic, sizeof(topic), &app->cfg) == 0) {
        mosquitto_subscribe(app->mqtt_main, NULL, topic, app->cfg.mqtt_command_subscribe_qos);
    }
    slgw_publish_availability_pair(app, app->transport_connected, app->transport_connected ? "connected" : "transport_down");
    slgw_flush_offline_queue(app);
}

static void slgw_mqtt_on_main_disconnect(struct mosquitto *mosq, void *userdata, int rc) {
    slgw_app *app = (slgw_app *)userdata;
    (void)mosq;
    app->mqtt_main_connected = false;
    printf("[SLGW][MQTT] main disconnected rc=%d\n", rc);
}

static void slgw_mqtt_on_availability_connect(struct mosquitto *mosq, void *userdata, int rc) {
    slgw_app *app = (slgw_app *)userdata;
    (void)mosq;
    app->mqtt_availability_connected = (rc == 0);
    printf("[SLGW][MQTT] availability connected rc=%d\n", rc);
    if (rc == 0) {
        slgw_publish_availability_pair(app, app->transport_connected, app->transport_connected ? "connected" : "transport_down");
    }
}

static void slgw_mqtt_on_availability_disconnect(struct mosquitto *mosq, void *userdata, int rc) {
    slgw_app *app = (slgw_app *)userdata;
    (void)mosq;
    app->mqtt_availability_connected = false;
    printf("[SLGW][MQTT] availability disconnected rc=%d\n", rc);
}

static void slgw_publish_invalid_command_ack(slgw_app *app, const char *topic, const char *message) {
    slgw_command_request request;
    memset(&request, 0, sizeof(request));
    request.valid = false;
    slgw_copy_text(request.cmd_name, sizeof(request.cmd_name), "unknown");
    if (topic != NULL) {
        char integration_topic[SLGW_TOPIC_SIZE];
        if (slgw_topic_integration_command(integration_topic, sizeof(integration_topic), &app->cfg) == 0 && strcmp(topic, integration_topic) == 0) {
            request.context.from_integration = true;
            slgw_copy_text(request.context.source_topic, sizeof(request.context.source_topic), topic);
        }
    }
    slgw_publish_ack_for_request(app, &request, false, message, NULL, "invalid_command_payload");
}

static void slgw_mqtt_on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg) {
    slgw_app *app = (slgw_app *)userdata;
    slgw_command_request request;
    char payload[SLGW_JSON_SIZE];
    (void)mosq;
    if (msg->payloadlen < 0 || msg->payloadlen >= (int)sizeof(payload)) {
        slgw_publish_invalid_command_ack(app, msg->topic, "payload_too_large");
        return;
    }
    memcpy(payload, msg->payload, (size_t)msg->payloadlen);
    payload[msg->payloadlen] = '\0';
    memset(&request, 0, sizeof(request));
    if (slgw_json_parse_command(msg->topic, payload, &app->cfg, &request) != 0) {
        const char *message = request.error_message[0] == '\0' ? "invalid_command_payload" : request.error_message;

        /* Preserve req_id / command for parameter-validation failures whenever the
         * parser was able to extract them before rejecting the payload. This lets
         * Server / Web / APP correlate negative ACKs, e.g. tone_bias_out_of_range.
         */
        if (request.context.request_id[0] != '\0' || request.cmd_name[0] != '\0') {
            if (request.cmd_name[0] == '\0') {
                slgw_copy_text(request.cmd_name, sizeof(request.cmd_name), "unknown");
            }
            if (request.context.source_topic[0] == '\0' && msg->topic != NULL) {
                slgw_copy_text(request.context.source_topic, sizeof(request.context.source_topic), msg->topic);
            }
            slgw_publish_ack_for_request(app, &request, false, message, NULL, "invalid_command_payload");
        } else {
            slgw_publish_invalid_command_ack(app, msg->topic, message);
        }
        return;
    }
    pthread_mutex_lock(&app->command_queue_mutex);
    if (slgw_enqueue_command_with_coalescing(app, &request) != 0) {
        pthread_mutex_unlock(&app->command_queue_mutex);
        slgw_publish_ack_for_request(app, &request, false, "command_queue_full", NULL, "queue_full");
        return;
    }
    pthread_cond_signal(&app->command_queue_cond);
    pthread_mutex_unlock(&app->command_queue_mutex);
}

int slgw_app_init(slgw_app *app) {
    char topic[SLGW_TOPIC_SIZE];
    char payload[SLGW_JSON_SIZE];
    char availability_client_id[SLGW_STR_MID];
    int rc;
    memset(app, 0, sizeof(*app));
    if (slgw_config_load(&app->cfg) != 0) {
        fprintf(stderr, "[SLGW][INIT] config load failed: %s\n", app->cfg.load_error);
        return 1;
    }
    slgw_config_print(&app->cfg);
    pthread_mutex_init(&app->command_queue_mutex, NULL);
    pthread_cond_init(&app->command_queue_cond, NULL);
    pthread_mutex_init(&app->publish_mutex, NULL);
    pthread_mutex_init(&app->protocol_mutex, NULL);
    pthread_mutex_init(&app->req_id_mutex, NULL);
    pthread_mutex_init(&app->pending.mutex, NULL);
    pthread_cond_init(&app->pending.cond, NULL);
    pthread_mutex_init(&app->offline_queue_mutex, NULL);
    pthread_mutex_init(&app->status_mutex, NULL);
    pthread_cond_init(&app->status_cond, NULL);
    app->next_req_seq = 1;
    app->pending_status_req_id[0] = '\0';
    app->pending_status_has_req_id = false;
    if (slgw_process_lock_init(&app->process_lock, &app->cfg) != 0 || slgw_process_lock_acquire(&app->process_lock) != 0) {
        fprintf(stderr, "[SLGW][INIT] process lock acquire failed path=%s\n", app->cfg.process_lock_file);
        return 2;
    }
    rc = slgw_transport_init(&app->transport, app, &app->cfg);
    if (rc != 0) {
        fprintf(stderr, "[SLGW][INIT] transport init failed mode=%s\n", app->cfg.transport_mode);
        return 3;
    }
    if (slgw_topic_mcu_status(topic, sizeof(topic), &app->cfg) != 0) {
        return 4;
    }
    slgw_publish_buffer_init(&app->offline_queue, app->cfg.offline_queue_max, topic);

    mosquitto_lib_init();
    app->mqtt_main = mosquitto_new(app->cfg.mqtt_client_id, app->cfg.mqtt_clean_session, app);
    if (app->mqtt_main == NULL) {
        return 5;
    }
    mosquitto_reconnect_delay_set(app->mqtt_main, (unsigned int)app->cfg.mqtt_reconnect_min_delay_s, (unsigned int)app->cfg.mqtt_reconnect_max_delay_s, true);
    if (app->cfg.mqtt_username[0] != '\0') {
        mosquitto_username_pw_set(app->mqtt_main, app->cfg.mqtt_username, app->cfg.mqtt_password[0] == '\0' ? NULL : app->cfg.mqtt_password);
    }
    if (slgw_topic_mcu_availability(topic, sizeof(topic), &app->cfg) != 0) {
        return 6;
    }
    if (slgw_copy_text(payload, sizeof(payload), "offline") != 0) {
        return 7;
    }
    mosquitto_will_set(app->mqtt_main, topic, (int)strlen(payload), payload, 1, true);
    mosquitto_connect_callback_set(app->mqtt_main, slgw_mqtt_on_main_connect);
    mosquitto_disconnect_callback_set(app->mqtt_main, slgw_mqtt_on_main_disconnect);
    mosquitto_message_callback_set(app->mqtt_main, slgw_mqtt_on_message);

    if (app->cfg.integration_enabled) {
        if (slgw_format(availability_client_id, sizeof(availability_client_id), "%s-availability", app->cfg.mqtt_client_id) != 0) {
            fprintf(stderr, "[SLGW][INIT] availability client_id too long\n");
            return 8;
        }
        app->mqtt_availability = mosquitto_new(availability_client_id, true, app);
        if (app->mqtt_availability == NULL) {
            return 9;
        }
        mosquitto_reconnect_delay_set(app->mqtt_availability, (unsigned int)app->cfg.mqtt_reconnect_min_delay_s, (unsigned int)app->cfg.mqtt_reconnect_max_delay_s, true);
        if (app->cfg.mqtt_username[0] != '\0') {
            mosquitto_username_pw_set(app->mqtt_availability, app->cfg.mqtt_username, app->cfg.mqtt_password[0] == '\0' ? NULL : app->cfg.mqtt_password);
        }
        if (slgw_topic_integration_availability(topic, sizeof(topic), &app->cfg) != 0) {
            return 10;
        }
        if (slgw_copy_text(payload, sizeof(payload), "offline") != 0) {
            return 11;
        }
        mosquitto_will_set(app->mqtt_availability, topic, (int)strlen(payload), payload, 1, true);
        mosquitto_connect_callback_set(app->mqtt_availability, slgw_mqtt_on_availability_connect);
        mosquitto_disconnect_callback_set(app->mqtt_availability, slgw_mqtt_on_availability_disconnect);
    }
    return 0;
}

int slgw_app_start(slgw_app *app) {
    int rc;
    unsigned long long start_ms = slgw_now_monotonic_ms();
    signal(SIGINT, slgw_handle_signal);
    signal(SIGTERM, slgw_handle_signal);
    slgw_signal_stop = 0;
    app->running = true;
    app->stop_requested = false;
    rc = slgw_transport_start(&app->transport);
    if (rc != 0) {
        fprintf(stderr, "[SLGW][START] transport start failed rc=%d\n", rc);
        return rc;
    }
    rc = pthread_create(&app->command_thread, NULL, slgw_command_worker_main, app);
    if (rc != 0) {
        fprintf(stderr, "[SLGW][START] command worker start failed rc=%d\n", rc);
        return rc;
    }
    rc = pthread_create(&app->status_thread, NULL, slgw_status_worker_main, app);
    if (rc != 0) {
        fprintf(stderr, "[SLGW][START] status worker start failed rc=%d\n", rc);
        return rc;
    }
    app->status_thread_started = true;
    rc = mosquitto_connect_async(app->mqtt_main, app->cfg.mqtt_host, app->cfg.mqtt_port, app->cfg.mqtt_keepalive);
    if (rc != MOSQ_ERR_SUCCESS) {
        return rc;
    }
    rc = mosquitto_loop_start(app->mqtt_main);
    if (rc != MOSQ_ERR_SUCCESS) {
        return rc;
    }
    if (app->mqtt_availability != NULL) {
        rc = mosquitto_connect_async(app->mqtt_availability, app->cfg.mqtt_host, app->cfg.mqtt_port, app->cfg.mqtt_keepalive);
        if (rc != MOSQ_ERR_SUCCESS) {
            return rc;
        }
        rc = mosquitto_loop_start(app->mqtt_availability);
        if (rc != MOSQ_ERR_SUCCESS) {
            return rc;
        }
    }
    while (!slgw_signal_stop && !app->stop_requested) {
        if (app->cfg.run_seconds > 0 && slgw_now_monotonic_ms() - start_ms >= (unsigned long long)app->cfg.run_seconds * 1000ULL) {
            break;
        }
        sleep(1);
    }
    return 0;
}

void slgw_app_stop(slgw_app *app, bool graceful) {
    if (app->stop_requested) {
        return;
    }
    app->stop_requested = true;
    app->running = false;
    if (graceful && app->mqtt_main_connected) {
        slgw_publish_availability_pair(app, false, "graceful_shutdown");
        slgw_sleep_ms(200);
    }
    pthread_mutex_lock(&app->command_queue_mutex);
    pthread_cond_signal(&app->command_queue_cond);
    pthread_mutex_unlock(&app->command_queue_mutex);
    pthread_mutex_lock(&app->status_mutex);
    pthread_cond_signal(&app->status_cond);
    pthread_mutex_unlock(&app->status_mutex);
    if (app->mqtt_main != NULL) {
        mosquitto_disconnect(app->mqtt_main);
        mosquitto_loop_stop(app->mqtt_main, true);
    }
    if (app->mqtt_availability != NULL) {
        mosquitto_disconnect(app->mqtt_availability);
        mosquitto_loop_stop(app->mqtt_availability, true);
    }
    if (app->command_thread) {
        pthread_join(app->command_thread, NULL);
        app->command_thread = 0;
    }
    if (app->status_thread_started) {
        pthread_join(app->status_thread, NULL);
        app->status_thread_started = false;
    }
    slgw_transport_stop(&app->transport);
}

void slgw_app_destroy(slgw_app *app) {
    if (app->mqtt_main != NULL) {
        mosquitto_destroy(app->mqtt_main);
        app->mqtt_main = NULL;
    }
    if (app->mqtt_availability != NULL) {
        mosquitto_destroy(app->mqtt_availability);
        app->mqtt_availability = NULL;
    }
    mosquitto_lib_cleanup();
    slgw_process_lock_release(&app->process_lock);
    pthread_mutex_destroy(&app->command_queue_mutex);
    pthread_cond_destroy(&app->command_queue_cond);
    pthread_mutex_destroy(&app->publish_mutex);
    pthread_mutex_destroy(&app->protocol_mutex);
    pthread_mutex_destroy(&app->req_id_mutex);
    pthread_mutex_destroy(&app->pending.mutex);
    pthread_cond_destroy(&app->pending.cond);
    pthread_mutex_destroy(&app->offline_queue_mutex);
    pthread_mutex_destroy(&app->status_mutex);
    pthread_cond_destroy(&app->status_cond);
}

void slgw_app_notify_transport_state(slgw_app *app, bool connected, const char *reason) {
    app->transport_connected = connected;
    if (app->mqtt_main_connected) {
        slgw_publish_availability_pair(app, connected, connected ? "transport_up" : (reason == NULL ? "transport_down" : reason));
    }
    if (connected) {
        slgw_request_status_sync(app, reason == NULL ? "transport_connected" : reason, NULL);
    }
}

static int slgw_find_next_frame_start(const char *line, size_t start_offset) {
    static const char *tokens[] = {"STATE,STATUS,", "RESP,", "EVENT,"};
    int found = -1;
    size_t idx;
    for (idx = 0; idx < sizeof(tokens) / sizeof(tokens[0]); ++idx) {
        const char *pos = strstr(line + start_offset, tokens[idx]);
        if (pos != NULL) {
            int offset = (int)(pos - line);
            if (offset > 0 && (found < 0 || offset < found)) {
                found = offset;
            }
        }
    }
    return found;
}

static void slgw_handle_single_driver_frame(slgw_app *app, const char *line) {
    slgw_resp_frame response;
    slgw_status_frame status;
    slgw_event_frame event_frame;
    if (line == NULL || line[0] == '\0') {
        return;
    }
    if (slgw_protocol_parse_resp(line, &response)) {
        pthread_mutex_lock(&app->pending.mutex);
        if (app->pending.active && strcmp(app->pending.req_id, response.req_id) == 0) {
            memcpy(&app->pending.response, &response, sizeof(response));
            app->pending.ready = true;
            pthread_cond_signal(&app->pending.cond);
        }
        pthread_mutex_unlock(&app->pending.mutex);
        return;
    }
    if (slgw_protocol_parse_status(line, &status)) {
        memcpy(&app->last_status, &status, sizeof(status));
        app->has_status = true;
        if (app->mqtt_main_connected) {
            char req_id[64];
            bool has_req = false;
            pthread_mutex_lock(&app->status_mutex);
            has_req = app->pending_status_has_req_id;
            if (has_req) { slgw_copy_text(req_id, sizeof(req_id), app->pending_status_req_id); }
            app->pending_status_has_req_id = false;
            app->pending_status_req_id[0] = '\0';
            pthread_mutex_unlock(&app->status_mutex);
            slgw_publish_status_pair(app, &status, has_req ? req_id : NULL);
        }
        return;
    }
    if (slgw_protocol_parse_event(line, &event_frame)) {
        if (app->mqtt_main_connected) {
            slgw_publish_event_pair(app, &event_frame);
        }
        return;
    }
    if (app->cfg.debug) {
        printf("[SLGW][TRANSPORT][UNHANDLED] %s\n", line);
    }
}

void slgw_app_handle_driver_line(slgw_app *app, const char *line) {
    char frame[SLGW_LINE_SIZE];
    size_t cursor = 0;
    size_t line_len;

    if (line == NULL || line[0] == '\0') {
        return;
    }

    line_len = strlen(line);
    while (cursor < line_len) {
        int next_offset = slgw_find_next_frame_start(line, cursor + 1);
        size_t frame_len = (next_offset > 0) ? (size_t)next_offset - cursor : line_len - cursor;
        if (frame_len == 0) {
            break;
        }
        if (frame_len + 1 > sizeof(frame)) {
            if (app->cfg.debug) {
                printf("[SLGW][TRANSPORT][UNHANDLED] frame_too_long\n");
            }
            return;
        }
        memcpy(frame, line + cursor, frame_len);
        frame[frame_len] = '\0';
        slgw_handle_single_driver_frame(app, frame);
        if (next_offset <= 0) {
            break;
        }
        cursor = (size_t)next_offset;
    }
}
