#include "uart_comm.h"

#include <string.h>

#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

// ==========================
// RX byte ring
// ==========================
static volatile uint8_t g_rx_ring[UART_COMM_RX_RING_SIZE];
static volatile uint16_t g_rx_ring_head = 0;
static volatile uint16_t g_rx_ring_tail = 0;

// ==========================
// line framing state
// ==========================
static char g_uart_rx_line_buffer[UART_COMM_LINE_BUFFER_SIZE];
static uint16_t g_uart_rx_line_index = 0;
static bool g_uart_rx_discard_until_newline = false;

// ==========================
// TX line queue
// ==========================
typedef struct
{
    uint16_t length;
    char data[UART_COMM_TX_LINE_MAX + 2];
} uart_tx_slot_t;

static uart_tx_slot_t g_tx_queue[UART_COMM_TX_QUEUE_DEPTH];
static volatile uint16_t g_tx_head = 0;
static volatile uint16_t g_tx_tail = 0;
static volatile uint16_t g_tx_count = 0;
static volatile uint16_t g_tx_offset = 0;

// ==========================
// stats
// ==========================
static volatile uart_comm_stats_t g_uart_stats;

static inline uint16_t ring_next(uint16_t index, uint16_t size)
{
    index++;
    if (index >= size)
    {
        index = 0;
    }
    return index;
}

static bool rx_ring_push_byte(uint8_t ch)
{
    uint16_t next_head = ring_next(g_rx_ring_head, UART_COMM_RX_RING_SIZE);

    if (next_head == g_rx_ring_tail)
    {
        g_uart_stats.rx_ring_overflow++;
        return false;
    }

    g_rx_ring[g_rx_ring_head] = ch;
    g_rx_ring_head = next_head;
    return true;
}

static bool rx_ring_pop_byte(uint8_t *out_byte)
{
    if (out_byte == NULL)
    {
        return false;
    }

    uint32_t irq_state = save_and_disable_interrupts();

    if (g_rx_ring_head == g_rx_ring_tail)
    {
        restore_interrupts(irq_state);
        return false;
    }

    *out_byte = g_rx_ring[g_rx_ring_tail];
    g_rx_ring_tail = ring_next(g_rx_ring_tail, UART_COMM_RX_RING_SIZE);

    restore_interrupts(irq_state);
    return true;
}

static bool tx_queue_push_line(const char *text)
{
    if (text == NULL)
    {
        return false;
    }

    size_t text_len = strnlen(text, UART_COMM_TX_LINE_MAX);
    bool need_newline = (text_len == 0 || text[text_len - 1] != '\n');

    uint32_t irq_state = save_and_disable_interrupts();

    if (g_tx_count >= UART_COMM_TX_QUEUE_DEPTH)
    {
        g_uart_stats.tx_queue_full++;
        g_uart_stats.tx_dropped_lines++;
        restore_interrupts(irq_state);
        return false;
    }

    uart_tx_slot_t *slot = &g_tx_queue[g_tx_head];
    memset(slot, 0, sizeof(*slot));

    memcpy(slot->data, text, text_len);
    slot->length = (uint16_t)text_len;

    if (need_newline)
    {
        slot->data[slot->length++] = '\n';
    }

    slot->data[slot->length] = '\0';

    g_tx_head = ring_next(g_tx_head, UART_COMM_TX_QUEUE_DEPTH);
    g_tx_count++;
    g_uart_stats.tx_enqueued_lines++;

    restore_interrupts(irq_state);
    return true;
}

static void uart_comm_rx_irq_handler(void)
{
    while (uart_is_readable(LIGHT_NODE_UART_ID))
    {
        uint8_t ch = (uint8_t)uart_getc(LIGHT_NODE_UART_ID);
        g_uart_stats.rx_irq_bytes++;
        rx_ring_push_byte(ch);
    }
}

bool uart_comm_init(void)
{
    uart_comm_clear_all();

    uart_init(LIGHT_NODE_UART_ID, LIGHT_NODE_UART_BAUDRATE);
    gpio_set_function(LIGHT_NODE_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(LIGHT_NODE_UART_RX_PIN, GPIO_FUNC_UART);

    uart_set_hw_flow(LIGHT_NODE_UART_ID, false, false);
    uart_set_format(LIGHT_NODE_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(LIGHT_NODE_UART_ID, true);

    while (uart_is_readable(LIGHT_NODE_UART_ID))
    {
        (void)uart_getc(LIGHT_NODE_UART_ID);
    }

    irq_set_exclusive_handler(LIGHT_NODE_UART_IRQ, uart_comm_rx_irq_handler);
    irq_set_enabled(LIGHT_NODE_UART_IRQ, true);
    uart_set_irq_enables(LIGHT_NODE_UART_ID, true, false);

    return true;
}

void uart_comm_task(void)
{
    while (uart_is_writable(LIGHT_NODE_UART_ID))
    {
        char out_ch = '\0';
        bool has_data = false;

        uint32_t irq_state = save_and_disable_interrupts();

        if (g_tx_count > 0)
        {
            uart_tx_slot_t *slot = &g_tx_queue[g_tx_tail];
            out_ch = slot->data[g_tx_offset++];
            has_data = true;

            if (g_tx_offset >= slot->length)
            {
                g_tx_offset = 0;
                g_tx_tail = ring_next(g_tx_tail, UART_COMM_TX_QUEUE_DEPTH);
                g_tx_count--;
                g_uart_stats.tx_sent_lines++;
            }
        }

        restore_interrupts(irq_state);

        if (!has_data)
        {
            break;
        }

        uart_putc_raw(LIGHT_NODE_UART_ID, out_ch);
    }
}

bool uart_comm_read_line(char *buffer, uint16_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0)
    {
        return false;
    }

    uint8_t ch = 0;

    while (rx_ring_pop_byte(&ch))
    {
        if (ch == '\r')
        {
            continue;
        }

        if (g_uart_rx_discard_until_newline)
        {
            if (ch == '\n')
            {
                g_uart_rx_discard_until_newline = false;
                g_uart_rx_line_index = 0;
                g_uart_stats.rx_discarded_lines++;
            }
            continue;
        }

        if (ch == '\n')
        {
            if (g_uart_rx_line_index == 0)
            {
                continue;
            }

            size_t copy_len = g_uart_rx_line_index;
            if (copy_len > (size_t)(buffer_size - 1))
            {
                copy_len = buffer_size - 1;
                g_uart_stats.rx_line_dropped++;
            }

            memcpy(buffer, g_uart_rx_line_buffer, copy_len);
            buffer[copy_len] = '\0';

            g_uart_rx_line_index = 0;
            return true;
        }

        if (g_uart_rx_line_index < (UART_COMM_LINE_BUFFER_SIZE - 1))
        {
            g_uart_rx_line_buffer[g_uart_rx_line_index++] = (char)ch;
        }
        else
        {
            g_uart_rx_line_index = 0;
            g_uart_rx_discard_until_newline = true;
            g_uart_stats.rx_line_overflow++;
        }
    }

    return false;
}

void uart_comm_write_line(const char *text)
{
    (void)tx_queue_push_line(text);
}

void uart_comm_clear_rx_state(void)
{
    uint32_t irq_state = save_and_disable_interrupts();

    g_rx_ring_head = 0;
    g_rx_ring_tail = 0;

    restore_interrupts(irq_state);

    g_uart_rx_line_index = 0;
    g_uart_rx_discard_until_newline = false;
    memset(g_uart_rx_line_buffer, 0, sizeof(g_uart_rx_line_buffer));
}

void uart_comm_clear_tx_queue(void)
{
    uint32_t irq_state = save_and_disable_interrupts();

    memset(g_tx_queue, 0, sizeof(g_tx_queue));
    g_tx_head = 0;
    g_tx_tail = 0;
    g_tx_count = 0;
    g_tx_offset = 0;

    restore_interrupts(irq_state);
}

void uart_comm_clear_all(void)
{
    uint32_t irq_state = save_and_disable_interrupts();

    memset((void *)g_rx_ring, 0, sizeof(g_rx_ring));
    g_rx_ring_head = 0;
    g_rx_ring_tail = 0;

    memset(g_tx_queue, 0, sizeof(g_tx_queue));
    g_tx_head = 0;
    g_tx_tail = 0;
    g_tx_count = 0;
    g_tx_offset = 0;

    memset((void *)&g_uart_stats, 0, sizeof(g_uart_stats));

    restore_interrupts(irq_state);

    memset(g_uart_rx_line_buffer, 0, sizeof(g_uart_rx_line_buffer));
    g_uart_rx_line_index = 0;
    g_uart_rx_discard_until_newline = false;
}

void uart_comm_get_stats(uart_comm_stats_t *out_stats)
{
    if (out_stats == NULL)
    {
        return;
    }

    uint32_t irq_state = save_and_disable_interrupts();
    memcpy(out_stats, (const void *)&g_uart_stats, sizeof(*out_stats));
    restore_interrupts(irq_state);
}
