#ifndef SLGW_TOPICS_H
#define SLGW_TOPICS_H

#include <stddef.h>

#include "slgw_config.h"

int slgw_topic_server_set(char *out, size_t out_size, const slgw_config *cfg);
int slgw_topic_mcu_ack(char *out, size_t out_size, const slgw_config *cfg);
int slgw_topic_mcu_status(char *out, size_t out_size, const slgw_config *cfg);
int slgw_topic_mcu_event(char *out, size_t out_size, const slgw_config *cfg);
int slgw_topic_mcu_availability(char *out, size_t out_size, const slgw_config *cfg);

int slgw_topic_integration_command(char *out, size_t out_size, const slgw_config *cfg);
int slgw_topic_integration_ack(char *out, size_t out_size, const slgw_config *cfg);
int slgw_topic_integration_status(char *out, size_t out_size, const slgw_config *cfg);
int slgw_topic_integration_event(char *out, size_t out_size, const slgw_config *cfg);
int slgw_topic_integration_availability(char *out, size_t out_size, const slgw_config *cfg);

#endif
