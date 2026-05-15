#ifndef SLGW_TRANSPORT_DRIVER_H
#define SLGW_TRANSPORT_DRIVER_H

#include <pthread.h>
#include <stdbool.h>

#include "slgw_config.h"

struct slgw_app;

typedef struct {
    int fd;
    bool running;
    bool connected;
    bool stop_requested;
    char device[SLGW_PATH_SIZE];
    int reconnect_delay_ms;
    pthread_t reader_thread;
    pthread_mutex_t io_mutex;
    struct slgw_app *app;
} slgw_driver_transport;

int slgw_driver_transport_init(slgw_driver_transport *transport, struct slgw_app *app, const slgw_config *cfg);
int slgw_driver_transport_start(slgw_driver_transport *transport);
void slgw_driver_transport_stop(slgw_driver_transport *transport);
int slgw_driver_transport_write_line(slgw_driver_transport *transport, const char *line);

#endif
