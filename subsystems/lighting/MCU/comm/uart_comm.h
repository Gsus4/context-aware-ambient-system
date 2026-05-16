#ifndef UART_COMM_H
#define UART_COMM_H

#include <stdbool.h>
#include <stdint.h>

#include "../config.h"

#define UART_COMM_BUFFER_SIZE UART_COMM_LINE_BUFFER_SIZE

typedef struct
{
    uint32_t rx_irq_bytes;
    uint32_t rx_ring_overflow;
    uint32_t rx_line_overflow;
    uint32_t rx_discarded_lines;
    uint32_t rx_line_dropped;

    uint32_t tx_enqueued_lines;
    uint32_t tx_sent_lines;
    uint32_t tx_queue_full;
    uint32_t tx_dropped_lines;
} uart_comm_stats_t;

bool uart_comm_init(void);
void uart_comm_task(void);

bool uart_comm_read_line(char *buffer, uint16_t buffer_size);
void uart_comm_write_line(const char *text);

void uart_comm_clear_rx_state(void);
void uart_comm_clear_tx_queue(void);
void uart_comm_clear_all(void);
void uart_comm_get_stats(uart_comm_stats_t *out_stats);

#endif
