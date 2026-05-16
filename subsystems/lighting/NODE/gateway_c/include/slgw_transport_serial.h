#ifndef SLGW_TRANSPORT_SERIAL_H
#define SLGW_TRANSPORT_SERIAL_H

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
    int baudrate;
    int reconnect_delay_ms;
    int read_timeout_ms;
    pthread_t reader_thread;
    pthread_mutex_t io_mutex;
    struct slgw_app *app;
} slgw_serial_transport;

int slgw_serial_transport_init(slgw_serial_transport *transport, struct slgw_app *app, const slgw_config *cfg);
int slgw_serial_transport_start(slgw_serial_transport *transport);
void slgw_serial_transport_stop(slgw_serial_transport *transport);
int slgw_serial_transport_write_line(slgw_serial_transport *transport, const char *line);

#endif
