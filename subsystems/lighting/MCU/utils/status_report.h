#ifndef STATUS_REPORT_H
#define STATUS_REPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "services/light_sensor_service.h"
#include "services/mode_service.h"
#include "utils/system_state.h"

void status_report_init(uint32_t session_id);
void status_report_set_system_state(system_state_t system_state);
void status_report_set_report_enabled(bool enabled);
uint32_t status_report_get_session_id(void);

void build_status_report(char *buffer,
                         uint16_t buffer_size,
                         mode_service_t *mode_service,
                         light_sensor_service_t *light_service);

void status_report_build_v5(char *buffer,
                            uint16_t buffer_size,
                            mode_service_t *mode_service,
                            light_sensor_service_t *light_service);

#endif
