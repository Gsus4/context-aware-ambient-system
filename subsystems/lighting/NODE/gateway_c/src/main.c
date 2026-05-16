#include <mosquitto.h>
#include <stdio.h>

#include "slgw_app.h"

int main(void) {
    slgw_app app;
    int rc;

    rc = slgw_app_init(&app);
    if (rc != 0) {
        fprintf(stderr, "[SLGW][MAIN] init failed rc=%d\n", rc);
        return 1;
    }

    rc = slgw_app_start(&app);
    if (rc != 0 && rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[SLGW][MAIN] start failed rc=%d\n", rc);
        slgw_app_stop(&app, false);
        slgw_app_destroy(&app);
        return 1;
    }

    slgw_app_stop(&app, true);
    slgw_app_destroy(&app);
    return 0;
}
