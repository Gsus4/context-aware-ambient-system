#include "slgw_publish_buffer.h"

#include <string.h>

#include "slgw_text.h"

static int slgw_publish_item_fill(slgw_publish_item *item, const char *topic, const char *payload, int qos, bool retain) {
    memset(item, 0, sizeof(*item));
    if (slgw_copy_text(item->topic, sizeof(item->topic), topic) != 0) {
        return -1;
    }
    if (slgw_copy_text(item->payload, sizeof(item->payload), payload) != 0) {
        return -1;
    }
    item->qos = qos;
    item->retain = retain;
    item->used = true;
    return 0;
}

void slgw_publish_buffer_init(slgw_publish_buffer *buffer, int max_items, const char *status_topic) {
    memset(buffer, 0, sizeof(*buffer));
    buffer->max_items = max_items;
    if (buffer->max_items <= 0 || buffer->max_items > SLGW_OFFLINE_QUEUE_CAPACITY) {
        buffer->max_items = SLGW_OFFLINE_QUEUE_CAPACITY;
    }
    slgw_copy_text(buffer->status_topic, sizeof(buffer->status_topic), status_topic == NULL ? "" : status_topic);
}

int slgw_publish_buffer_enqueue(slgw_publish_buffer *buffer, const char *topic, const char *payload, int qos, bool retain) {
    int index;
    if (buffer == NULL || topic == NULL || payload == NULL) {
        return -1;
    }
    if (buffer->status_topic[0] != '\0' && strcmp(topic, buffer->status_topic) == 0) {
        if (buffer->status_item_used) {
            buffer->replaced_status += 1;
        }
        return slgw_publish_item_fill(&buffer->status_item, topic, payload, qos, retain);
    }
    if (buffer->count >= buffer->max_items) {
        buffer->head = (buffer->head + 1) % SLGW_OFFLINE_QUEUE_CAPACITY;
        buffer->count -= 1;
        buffer->dropped_items += 1;
    }
    index = (buffer->head + buffer->count) % SLGW_OFFLINE_QUEUE_CAPACITY;
    if (slgw_publish_item_fill(&buffer->items[index], topic, payload, qos, retain) != 0) {
        return -1;
    }
    buffer->count += 1;
    return 0;
}

int slgw_publish_buffer_pop(slgw_publish_buffer *buffer, slgw_publish_item *out_item) {
    if (buffer == NULL || out_item == NULL) {
        return -1;
    }
    if (buffer->count > 0) {
        memcpy(out_item, &buffer->items[buffer->head], sizeof(*out_item));
        buffer->items[buffer->head].used = false;
        buffer->head = (buffer->head + 1) % SLGW_OFFLINE_QUEUE_CAPACITY;
        buffer->count -= 1;
        return 1;
    }
    if (buffer->status_item.used) {
        memcpy(out_item, &buffer->status_item, sizeof(*out_item));
        memset(&buffer->status_item, 0, sizeof(buffer->status_item));
        return 1;
    }
    memset(out_item, 0, sizeof(*out_item));
    return 0;
}
