#ifndef SLGW_APP_H
#define SLGW_APP_H

#include <mosquitto.h>
#include <pthread.h>
#include <stdbool.h>

#include "slgw_config.h"
#include "slgw_json.h"
#include "slgw_process_lock.h"
#include "slgw_protocol.h"
#include "slgw_publish_buffer.h"
#include "slgw_transport.h"

typedef struct {
    bool active;
    bool ready;
    char req_id[64];
    slgw_resp_frame response;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} slgw_pending_request;

typedef struct {
    bool occupied;
    slgw_command_request request;
} slgw_command_slot;

typedef struct slgw_app {
    slgw_config cfg;
    slgw_process_lock process_lock;

    struct mosquitto *mqtt_main;
    struct mosquitto *mqtt_availability;
    bool mqtt_main_connected;
    bool mqtt_availability_connected;
    bool transport_connected;

    bool running;
    bool stop_requested;

    slgw_transport transport;

    slgw_status_frame last_status;
    bool has_status;

    pthread_mutex_t command_queue_mutex;
    pthread_cond_t command_queue_cond;
    slgw_command_slot command_queue[SLGW_COMMAND_QUEUE_SIZE];
    int command_queue_head;
    int command_queue_tail;
    int command_queue_count;
    unsigned long long scene_coalesce_until_ms;
    pthread_t command_thread;

    pthread_mutex_t publish_mutex;
    pthread_mutex_t protocol_mutex;
    pthread_mutex_t req_id_mutex;
    unsigned int next_req_seq;

    slgw_pending_request pending;

    pthread_mutex_t offline_queue_mutex;
    slgw_publish_buffer offline_queue;

    pthread_mutex_t status_mutex;
    pthread_cond_t status_cond;
    pthread_t status_thread;
    bool status_thread_started;
    bool startup_status_done;
    bool pending_status_sync;
    char pending_status_reason[64];
    char pending_status_req_id[64];
    bool pending_status_has_req_id;
    unsigned long long last_status_request_ms;
} slgw_app;

int slgw_app_init(slgw_app *app);
int slgw_app_start(slgw_app *app);
void slgw_app_stop(slgw_app *app, bool graceful);
void slgw_app_destroy(slgw_app *app);

void slgw_app_handle_driver_line(slgw_app *app, const char *line);
void slgw_app_notify_transport_state(slgw_app *app, bool connected, const char *reason);

#endif
