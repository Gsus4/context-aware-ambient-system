#ifndef SLGW_TRANSPORT_H
#define SLGW_TRANSPORT_H

#include "slgw_config.h"
#include "slgw_transport_driver.h"
#include "slgw_transport_serial.h"

struct slgw_app;

typedef enum {
    SLGW_TRANSPORT_KIND_NONE = 0,
    SLGW_TRANSPORT_KIND_DRIVER,
    SLGW_TRANSPORT_KIND_SERIAL
} slgw_transport_kind;

typedef struct {
    slgw_transport_kind kind;
    slgw_driver_transport driver;
    slgw_serial_transport serial;
} slgw_transport;

int slgw_transport_init(slgw_transport *transport, struct slgw_app *app, const slgw_config *cfg);
int slgw_transport_start(slgw_transport *transport);
void slgw_transport_stop(slgw_transport *transport);
int slgw_transport_write_line(slgw_transport *transport, const char *line);

#endif
