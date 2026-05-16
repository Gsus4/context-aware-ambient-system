#include "slgw_transport.h"

#include <string.h>

int slgw_transport_init(slgw_transport *transport, struct slgw_app *app, const slgw_config *cfg) {
    memset(transport, 0, sizeof(*transport));
    if (strcmp(cfg->transport_mode, "driver") == 0) {
        transport->kind = SLGW_TRANSPORT_KIND_DRIVER;
        return slgw_driver_transport_init(&transport->driver, app, cfg);
    }
    if (strcmp(cfg->transport_mode, "serial") == 0) {
        transport->kind = SLGW_TRANSPORT_KIND_SERIAL;
        return slgw_serial_transport_init(&transport->serial, app, cfg);
    }
    return -1;
}

int slgw_transport_start(slgw_transport *transport) {
    if (transport->kind == SLGW_TRANSPORT_KIND_DRIVER) {
        return slgw_driver_transport_start(&transport->driver);
    }
    if (transport->kind == SLGW_TRANSPORT_KIND_SERIAL) {
        return slgw_serial_transport_start(&transport->serial);
    }
    return -1;
}

void slgw_transport_stop(slgw_transport *transport) {
    if (transport->kind == SLGW_TRANSPORT_KIND_DRIVER) {
        slgw_driver_transport_stop(&transport->driver);
    } else if (transport->kind == SLGW_TRANSPORT_KIND_SERIAL) {
        slgw_serial_transport_stop(&transport->serial);
    }
}

int slgw_transport_write_line(slgw_transport *transport, const char *line) {
    if (transport->kind == SLGW_TRANSPORT_KIND_DRIVER) {
        return slgw_driver_transport_write_line(&transport->driver, line);
    }
    if (transport->kind == SLGW_TRANSPORT_KIND_SERIAL) {
        return slgw_serial_transport_write_line(&transport->serial, line);
    }
    return -1;
}
