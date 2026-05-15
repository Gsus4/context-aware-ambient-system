#include "slgw_topics.h"

#include "slgw_text.h"

static int slgw_topic_join(char *out, size_t out_size, const char *base, const char *node_id, const char *leaf) {
    return slgw_format(out, out_size, "%s/%s/%s", base, node_id, leaf);
}

int slgw_topic_server_set(char *out, size_t out_size, const slgw_config *cfg) {
    return slgw_topic_join(out, out_size, cfg->mqtt_internal_topic_base, cfg->node_id, "cmd");
}
int slgw_topic_mcu_ack(char *out, size_t out_size, const slgw_config *cfg) {
    return slgw_topic_join(out, out_size, cfg->mqtt_internal_topic_base, cfg->node_id, "ack");
}
int slgw_topic_mcu_status(char *out, size_t out_size, const slgw_config *cfg) {
    return slgw_topic_join(out, out_size, cfg->mqtt_internal_topic_base, cfg->node_id, "status");
}
int slgw_topic_mcu_event(char *out, size_t out_size, const slgw_config *cfg) {
    return slgw_topic_join(out, out_size, cfg->mqtt_internal_topic_base, cfg->node_id, "event");
}
int slgw_topic_mcu_availability(char *out, size_t out_size, const slgw_config *cfg) {
    return slgw_topic_join(out, out_size, cfg->mqtt_internal_topic_base, cfg->node_id, "availability");
}
int slgw_topic_integration_command(char *out, size_t out_size, const slgw_config *cfg) { return slgw_topic_server_set(out,out_size,cfg); }
int slgw_topic_integration_ack(char *out, size_t out_size, const slgw_config *cfg) { return slgw_topic_mcu_ack(out,out_size,cfg); }
int slgw_topic_integration_status(char *out, size_t out_size, const slgw_config *cfg) { return slgw_topic_mcu_status(out,out_size,cfg); }
int slgw_topic_integration_event(char *out, size_t out_size, const slgw_config *cfg) { return slgw_topic_mcu_event(out,out_size,cfg); }
int slgw_topic_integration_availability(char *out, size_t out_size, const slgw_config *cfg) { return slgw_topic_mcu_availability(out,out_size,cfg); }
