#ifndef SLGW_PUBLISH_BUFFER_H
#define SLGW_PUBLISH_BUFFER_H

#include <stdbool.h>
#include <stddef.h>

#include "slgw_config.h"

typedef struct {
    bool used;
    char topic[SLGW_TOPIC_SIZE];
    char payload[SLGW_JSON_SIZE];
    int qos;
    bool retain;
} slgw_publish_item;

typedef struct {
    slgw_publish_item items[SLGW_OFFLINE_QUEUE_CAPACITY];
    int head;
    int count;
    int max_items;
    int dropped_items;
    int replaced_status;
    char status_topic[SLGW_TOPIC_SIZE];
    bool status_item_used;
    slgw_publish_item status_item;
} slgw_publish_buffer;

void slgw_publish_buffer_init(slgw_publish_buffer *buffer, int max_items, const char *status_topic);
int slgw_publish_buffer_enqueue(slgw_publish_buffer *buffer, const char *topic, const char *payload, int qos, bool retain);
int slgw_publish_buffer_pop(slgw_publish_buffer *buffer, slgw_publish_item *out_item);

#endif
