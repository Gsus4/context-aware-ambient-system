#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "pico/time.h"

#include "config.h"

#include "drivers/bh1750.h"
#include "drivers/ws2812.h"

#include "services/light_sensor_service.h"
#include "services/led_service.h"
#include "services/auto_light_service.h"
#include "services/flow_service.h"
#include "services/mode_service.h"
#include "services/command_parser.h"

#include "utils/logger.h"
#include "utils/status_report.h"
#include "utils/error_code.h"
#include "utils/system_state.h"

#include "comm/uart_comm.h"

#define MAIN_LOOP_DELAY_MS 1
#define REPORT_INTERVAL_US 1000000
#define USB_WAIT_RETRY_COUNT 50
#define USB_WAIT_DELAY_MS 100
#define READY_PRE_DELAY_MS 300
#define READY_POST_DELAY_MS 500
#define STATUS_BUFFER_SIZE 768

typedef enum
{
    COMMAND_SOURCE_USB = 0,
    COMMAND_SOURCE_UART
} command_source_t;

static bool g_uart_ready = false;


static void service_watchdog(void)
{
#if LIGHT_NODE_WATCHDOG_ENABLE
    watchdog_update();
#endif
}

static void init_watchdog_protection(void)
{
#if LIGHT_NODE_WATCHDOG_ENABLE
    if (!watchdog_caused_reboot())
    {
        log_info("Watchdog enabled");
    }
    else
    {
        log_info("Watchdog caused previous reboot");
    }
    watchdog_enable(LIGHT_NODE_WATCHDOG_TIMEOUT_MS, true);
    watchdog_update();
#endif
}

static void init_i2c_bus(void)
{
    i2c_init(LIGHT_NODE_I2C_PORT, LIGHT_NODE_I2C_BAUDRATE);
    gpio_set_function(LIGHT_NODE_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(LIGHT_NODE_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(LIGHT_NODE_I2C_SDA_PIN);
    gpio_pull_up(LIGHT_NODE_I2C_SCL_PIN);
}

static void fatal_error(error_code_t code, const char *message)
{
    char errbuf[128];

    log_error(message);
    snprintf(errbuf, sizeof(errbuf), "EVENT,ERROR,%s,%s,%lu",
             error_code_to_id(code),
             error_code_to_string(code),
             (unsigned long)to_ms_since_boot(get_absolute_time()));

    printf("%s\n", errbuf);
    if (g_uart_ready)
    {
        uart_comm_write_line(errbuf);
    }

    while (true)
    {
        if (g_uart_ready)
        {
            uart_comm_task();
        }
        sleep_ms(10);
    }
}

static bool line_has_non_space(const char *buffer, uint16_t length)
{
    if (buffer == NULL)
    {
        return false;
    }

    for (uint16_t i = 0; i < length; i++)
    {
        if (!isspace((unsigned char)buffer[i]))
        {
            return true;
        }
    }

    return false;
}

static void wait_for_usb_ready(void)
{
    for (int i = 0; i < USB_WAIT_RETRY_COUNT; i++)
    {
        if (stdio_usb_connected())
        {
            break;
        }
        sleep_ms(USB_WAIT_DELAY_MS);
    }
}

static void flush_usb_rx_buffer(void)
{
    while (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT)
    {
    }
}


static void sleep_with_uart_task(uint32_t delay_ms)
{
    absolute_time_t start_time = get_absolute_time();

    while (absolute_time_diff_us(start_time, get_absolute_time()) < ((int64_t)delay_ms * 1000))
    {
        uart_comm_task();
        service_watchdog();
        sleep_ms(1);
    }
}


static void print_ready(void)
{
    char ready_buffer[64];
    snprintf(ready_buffer, sizeof(ready_buffer), "EVENT,BOOT,OK,READY,%lu",
             (unsigned long)to_ms_since_boot(get_absolute_time()));
    if (LIGHT_NODE_USB_READY_MIRROR)
    {
        printf("%s\n", ready_buffer);
    }
    uart_comm_write_line(ready_buffer);
}

static void print_status_report_usb(mode_service_t *mode_service,
                                    light_sensor_service_t *light_service)
{
    char status_buffer[STATUS_BUFFER_SIZE];
    memset(status_buffer, 0, sizeof(status_buffer));

    build_status_report(status_buffer,
                        sizeof(status_buffer),
                        mode_service,
                        light_service);

    if (status_buffer[0] != '\0' && LIGHT_NODE_USB_STATUS_MIRROR)
    {
        printf("%s\n", status_buffer);
    }
}

static void print_status_report_uart(mode_service_t *mode_service,
                                     light_sensor_service_t *light_service)
{
    char status_buffer[STATUS_BUFFER_SIZE];
    memset(status_buffer, 0, sizeof(status_buffer));

    build_status_report(status_buffer,
                        sizeof(status_buffer),
                        mode_service,
                        light_service);

    if (status_buffer[0] != '\0')
    {
        uart_comm_write_line(status_buffer);
    }
}

static void send_response(command_source_t source, const char *response)
{
    if (response == NULL || response[0] == '\0')
    {
        return;
    }

    if (source == COMMAND_SOURCE_USB)
    {
        printf("%s\n", response);
    }
    else
    {
        uart_comm_write_line(response);
    }
}

static void broadcast_event(const char *event_line)
{
    if (event_line == NULL || event_line[0] == '\0')
    {
        return;
    }

    if (LIGHT_NODE_USB_EVENT_MIRROR)
    {
        printf("%s\n", event_line);
    }
    uart_comm_write_line(event_line);
}

static void emit_uart_stat_events(void)
{
    static uart_comm_stats_t last_stats = {0};
    uart_comm_stats_t now_stats;
    char event_buffer[96];

    uart_comm_get_stats(&now_stats);

    if (now_stats.rx_ring_overflow != last_stats.rx_ring_overflow)
    {
        snprintf(event_buffer, sizeof(event_buffer),
                 "EVENT,UART_WARN,EXEC_ERROR,RX_RING_OVERFLOW,%lu",
                 (unsigned long)now_stats.rx_ring_overflow);
        broadcast_event(event_buffer);
        last_stats.rx_ring_overflow = now_stats.rx_ring_overflow;
    }

    if (now_stats.rx_line_overflow != last_stats.rx_line_overflow)
    {
        snprintf(event_buffer, sizeof(event_buffer),
                 "EVENT,UART_WARN,EXEC_ERROR,RX_LINE_OVERFLOW,%lu",
                 (unsigned long)now_stats.rx_line_overflow);
        broadcast_event(event_buffer);
        last_stats.rx_line_overflow = now_stats.rx_line_overflow;
    }

    if (now_stats.rx_discarded_lines != last_stats.rx_discarded_lines)
    {
        snprintf(event_buffer, sizeof(event_buffer),
                 "EVENT,UART_INFO,OK,RX_DISCARDED_LINES,%lu",
                 (unsigned long)now_stats.rx_discarded_lines);
        broadcast_event(event_buffer);
        last_stats.rx_discarded_lines = now_stats.rx_discarded_lines;
    }

    if (now_stats.rx_line_dropped != last_stats.rx_line_dropped)
    {
        snprintf(event_buffer, sizeof(event_buffer),
                 "EVENT,UART_WARN,EXEC_ERROR,RX_LINE_DROPPED,%lu",
                 (unsigned long)now_stats.rx_line_dropped);
        broadcast_event(event_buffer);
        last_stats.rx_line_dropped = now_stats.rx_line_dropped;
    }

    if (now_stats.tx_queue_full != last_stats.tx_queue_full)
    {
        snprintf(event_buffer, sizeof(event_buffer),
                 "EVENT,UART_WARN,EXEC_ERROR,TX_QUEUE_FULL,%lu",
                 (unsigned long)now_stats.tx_queue_full);
        broadcast_event(event_buffer);
        last_stats.tx_queue_full = now_stats.tx_queue_full;
    }

    if (now_stats.tx_dropped_lines != last_stats.tx_dropped_lines)
    {
        snprintf(event_buffer, sizeof(event_buffer),
                 "EVENT,UART_WARN,EXEC_ERROR,TX_DROPPED_LINES,%lu",
                 (unsigned long)now_stats.tx_dropped_lines);
        broadcast_event(event_buffer);
        last_stats.tx_dropped_lines = now_stats.tx_dropped_lines;
    }
}


static void emit_status_to_source(command_source_t source,
                                  mode_service_t *mode_service,
                                  light_sensor_service_t *light_service)
{
    char status_buffer[STATUS_BUFFER_SIZE];
    memset(status_buffer, 0, sizeof(status_buffer));

    build_status_report(status_buffer, sizeof(status_buffer), mode_service, light_service);
    send_response(source, status_buffer);
}

static void emit_events_from_result(const command_parser_result_t *result,
                                    mode_service_t *mode_service,
                                    bool report_enabled)
{
    char event_buffer[192];
    (void)report_enabled;
    if (result == NULL || !result->success)
    {
        return;
    }

    if (result->mode_changed)
    {
        snprintf(event_buffer, sizeof(event_buffer),
                 "EVENT,MODE_CHANGED,OK,%s_APPLIED,%lu",
                 light_mode_to_string(mode_service_get_mode(mode_service)),
                 (unsigned long)to_ms_since_boot(get_absolute_time()));
        broadcast_event(event_buffer);
    }

    if (result->scene_changed)
    {
        snprintf(event_buffer, sizeof(event_buffer),
                 "EVENT,MODE_CHANGED,OK,SCENE_%s_APPLIED,%lu",
                 scene_mode_to_string(mode_service_get_scene(mode_service)),
                 (unsigned long)to_ms_since_boot(get_absolute_time()));
        broadcast_event(event_buffer);
    }

    if (result->flow_preset_changed || result->flow_speed_changed || result->flow_brightness_changed)
    {
        snprintf(event_buffer, sizeof(event_buffer),
                 "EVENT,MODE_CHANGED,OK,FLOW_%s_APPLIED,%lu",
                 flow_preset_to_string(mode_service_get_flow_preset(mode_service)),
                 (unsigned long)to_ms_since_boot(get_absolute_time()));
        broadcast_event(event_buffer);
    }

    if (result->breathing_changed)
    {
        snprintf(event_buffer, sizeof(event_buffer),
                 "EVENT,MODE_CHANGED,OK,STATIC_BREATHING_UPDATED,%lu",
                 (unsigned long)to_ms_since_boot(get_absolute_time()));
        broadcast_event(event_buffer);
    }

    if (result->power_changed)
    {
        snprintf(event_buffer, sizeof(event_buffer),
                 "EVENT,MODE_CHANGED,OK,POWER_OFF,%lu",
                 (unsigned long)to_ms_since_boot(get_absolute_time()));
        broadcast_event(event_buffer);
    }
}

static bool process_command_line(const char *line,
                                 command_source_t source,
                                 char *response_buffer,
                                 uint16_t response_size,
                                 mode_service_t *mode_service,
                                 light_sensor_service_t *light_service,
                                 bool *report_enabled)
{
    char local_line[COMMAND_BUFFER_SIZE];
    command_parser_result_t parse_result;

    if (line == NULL ||
        response_buffer == NULL ||
        mode_service == NULL ||
        light_service == NULL ||
        report_enabled == NULL)
    {
        return false;
    }

    memset(response_buffer, 0, response_size);
    memset(&parse_result, 0, sizeof(parse_result));
    strncpy(local_line, line, sizeof(local_line) - 1);
    local_line[sizeof(local_line) - 1] = '\0';

    if (!line_has_non_space(local_line, (uint16_t)strlen(local_line)))
    {
        return false;
    }

    command_parser_process_line(local_line,
                                response_buffer,
                                response_size,
                                mode_service,
                                light_service,
                                report_enabled,
                                &parse_result);

    send_response(source, response_buffer);
    emit_events_from_result(&parse_result, mode_service, *report_enabled);

    if (parse_result.success && parse_result.should_emit_status)
    {
        emit_status_to_source(source, mode_service, light_service);
    }

    return true;
}

static bool process_usb_serial_input(char *command_buffer,
                                     uint16_t *command_index,
                                     char *response_buffer,
                                     uint16_t response_size,
                                     mode_service_t *mode_service,
                                     light_sensor_service_t *light_service,
                                     bool *report_enabled)
{
    if (command_buffer == NULL ||
        command_index == NULL ||
        response_buffer == NULL ||
        mode_service == NULL ||
        light_service == NULL ||
        report_enabled == NULL)
    {
        return false;
    }

    int ch = getchar_timeout_us(0);
    if (ch == PICO_ERROR_TIMEOUT)
    {
        return false;
    }

    if (ch == '\r')
    {
        return false;
    }

    if (ch == '\n')
    {
        if (*command_index > 0)
        {
            command_buffer[*command_index] = '\0';
            process_command_line(command_buffer,
                                 COMMAND_SOURCE_USB,
                                 response_buffer,
                                 response_size,
                                 mode_service,
                                 light_service,
                                 report_enabled);
            *command_index = 0;
            return true;
        }

        return false;
    }

    if (*command_index < (COMMAND_BUFFER_SIZE - 1))
    {
        command_buffer[(*command_index)++] = (char)ch;
    }
    else
    {
        *command_index = 0;
        memset(response_buffer, 0, response_size);
        snprintf(response_buffer,
                 response_size,
                 "EVENT,ERROR,%s,%s,%lu",
                 error_code_to_id(ERROR_CODE_BUFFER_OVERFLOW),
                 error_code_to_string(ERROR_CODE_BUFFER_OVERFLOW),
                 (unsigned long)to_ms_since_boot(get_absolute_time()));
        send_response(COMMAND_SOURCE_USB, response_buffer);
    }

    return false;
}

static void init_light_sensor_system(light_sensor_service_t *light_service,
                                     bh1750_t *bh1750_device)
{
    if (light_service == NULL || bh1750_device == NULL)
    {
        fatal_error(ERROR_CODE_INTERNAL, "Light sensor init args invalid");
    }

    if (!light_sensor_service_init(light_service, bh1750_device))
    {
        fatal_error(ERROR_CODE_SENSOR_INIT_FAIL, "BH1750 init failed");
    }

    log_info("BH1750 init success");
}

static void init_led_system(led_service_t *led_service,
                            ws2812_t *ws2812_device)
{
    if (led_service == NULL || ws2812_device == NULL)
    {
        fatal_error(ERROR_CODE_INTERNAL, "LED init args invalid");
    }

    if (!ws2812_init_device(ws2812_device,
                            WS2812_PIO_INSTANCE,
                            WS2812_SM,
                            WS2812_DATA_PIN,
                            WS2812_LED_COUNT))
    {
        fatal_error(ERROR_CODE_LED_INIT_FAIL, "WS2812 init failed");
    }

    if (!led_service_init(led_service,
                          ws2812_device,
                          WS2812_DEFAULT_BRIGHTNESS))
    {
        fatal_error(ERROR_CODE_INTERNAL, "LED service init failed");
    }

    log_info("WS2812 init success");
}

static void init_auto_light_system(auto_light_service_t *auto_light_service,
                                   light_sensor_service_t *light_service,
                                   led_service_t *led_service)
{
    if (auto_light_service == NULL || light_service == NULL || led_service == NULL)
    {
        fatal_error(ERROR_CODE_INTERNAL, "Auto light init args invalid");
    }

    if (!auto_light_service_init(auto_light_service,
                                 light_service,
                                 led_service))
    {
        fatal_error(ERROR_CODE_INTERNAL, "Auto light service init failed");
    }

    log_info("Auto light service init success");
}

static void init_flow_system(flow_service_t *flow_service,
                             led_service_t *led_service)
{
    if (flow_service == NULL || led_service == NULL)
    {
        fatal_error(ERROR_CODE_INTERNAL, "Flow init args invalid");
    }

    if (!flow_service_init(flow_service, led_service))
    {
        fatal_error(ERROR_CODE_INTERNAL, "Flow service init failed");
    }

    log_info("Flow service init success");
}

static void init_mode_system(mode_service_t *mode_service,
                             auto_light_service_t *auto_light_service,
                             led_service_t *led_service,
                             flow_service_t *flow_service)
{
    if (mode_service == NULL ||
        auto_light_service == NULL ||
        led_service == NULL ||
        flow_service == NULL)
    {
        fatal_error(ERROR_CODE_INTERNAL, "Mode init args invalid");
    }

    if (!mode_service_init(mode_service,
                           auto_light_service,
                           led_service,
                           flow_service))
    {
        fatal_error(ERROR_CODE_INTERNAL, "Mode service init failed");
    }

    if (!mode_service_set_scene(mode_service, LIGHT_SCENE_WORK))
    {
        fatal_error(ERROR_CODE_INTERNAL, "Set default mode failed");
    }

    log_info("Mode service init success");
}

static void periodic_update_lux(light_sensor_service_t *light_service,
                                absolute_time_t *last_sensor_read_time)
{
    if (light_service == NULL || last_sensor_read_time == NULL)
    {
        return;
    }

    if (absolute_time_diff_us(*last_sensor_read_time, get_absolute_time()) <
        ((int64_t)SENSOR_READ_INTERVAL_MS * 1000))
    {
        return;
    }

    float lux = 0.0f;
    light_sensor_service_read_lux(light_service, &lux);
    *last_sensor_read_time = get_absolute_time();
}

int main(void)
{
    stdio_init_all();
    wait_for_usb_ready();
    sleep_ms(READY_PRE_DELAY_MS);
    log_info("Light node startup");
    init_watchdog_protection();

    init_i2c_bus();

    bh1750_t bh1750_device = {
        .i2c_port = LIGHT_NODE_I2C_PORT,
        .address = BH1750_I2C_ADDRESS
    };

    light_sensor_service_t light_service;
    ws2812_t ws2812_device;
    led_service_t led_service;
    auto_light_service_t auto_light_service;
    flow_service_t flow_service;
    mode_service_t mode_service;
    system_state_t system_state = SYSTEM_STATE_INIT;

    uint32_t session_id = (uint32_t)(time_us_32() ^ 0x5A17C3D1u);
    status_report_init(session_id);
    status_report_set_system_state(system_state);

    if (!uart_comm_init())
    {
        fatal_error(ERROR_CODE_UART_INIT_FAIL, "UART init failed");
    }
    g_uart_ready = true;
    log_info("UART init success");

    init_light_sensor_system(&light_service, &bh1750_device);
    init_led_system(&led_service, &ws2812_device);
    init_auto_light_system(&auto_light_service, &light_service, &led_service);
    init_flow_system(&flow_service, &led_service);
    init_mode_system(&mode_service, &auto_light_service, &led_service, &flow_service);

    char usb_command_buffer[COMMAND_BUFFER_SIZE];
    memset(usb_command_buffer, 0, sizeof(usb_command_buffer));

    char uart_command_buffer[COMMAND_BUFFER_SIZE];
    memset(uart_command_buffer, 0, sizeof(uart_command_buffer));

    char response_buffer[COMMAND_RESPONSE_SIZE];
    memset(response_buffer, 0, sizeof(response_buffer));

    uint16_t usb_command_index = 0;
    bool report_enabled = false;

    absolute_time_t last_report_time = get_absolute_time();
    absolute_time_t last_sensor_read_time = get_absolute_time();

    system_state = SYSTEM_STATE_READY;
    status_report_set_system_state(system_state);
    log_info(system_state_to_string(system_state));

    print_ready();
    sleep_with_uart_task(READY_POST_DELAY_MS);
    flush_usb_rx_buffer();

    system_state = SYSTEM_STATE_RUNNING;
    status_report_set_system_state(system_state);
    log_info(system_state_to_string(system_state));

    while (true)
    {
        service_watchdog();
        uart_comm_task();

        process_usb_serial_input(usb_command_buffer,
                                 &usb_command_index,
                                 response_buffer,
                                 sizeof(response_buffer),
                                 &mode_service,
                                 &light_service,
                                 &report_enabled);

        while (uart_comm_read_line(uart_command_buffer, sizeof(uart_command_buffer)))
        {
            process_command_line(uart_command_buffer,
                                 COMMAND_SOURCE_UART,
                                 response_buffer,
                                 sizeof(response_buffer),
                                 &mode_service,
                                 &light_service,
                                 &report_enabled);
            memset(uart_command_buffer, 0, sizeof(uart_command_buffer));
            uart_comm_task();
            service_watchdog();
        }

        periodic_update_lux(&light_service, &last_sensor_read_time);

        if (!mode_service_update(&mode_service))
        {
            if (system_state != SYSTEM_STATE_ERROR)
            {
                system_state = SYSTEM_STATE_ERROR;
                status_report_set_system_state(system_state);
                log_error("Mode update failed");

                memset(response_buffer, 0, sizeof(response_buffer));
                snprintf(response_buffer,
                         sizeof(response_buffer),
                         "EVENT,ERROR,%s,%s,%lu",
                         error_code_to_id(ERROR_CODE_MODE_UPDATE_FAIL),
                         error_code_to_string(ERROR_CODE_MODE_UPDATE_FAIL),
                         (unsigned long)to_ms_since_boot(get_absolute_time()));

                printf("%s\n", response_buffer);
                uart_comm_write_line(response_buffer);
            }
        }
        else if (system_state == SYSTEM_STATE_ERROR)
        {
            system_state = SYSTEM_STATE_RUNNING;
            status_report_set_system_state(system_state);
            log_info("Mode update recovered");
        }

        {
            char sensor_reason[32];
            if (auto_light_service_consume_error(&auto_light_service, sensor_reason, sizeof(sensor_reason)))
            {
                memset(response_buffer, 0, sizeof(response_buffer));
                snprintf(response_buffer,
                         sizeof(response_buffer),
                         "EVENT,SENSOR_ERROR,SENSOR_ERROR,%s,%lu",
                         sensor_reason,
                         (unsigned long)to_ms_since_boot(get_absolute_time()));
                broadcast_event(response_buffer);
            }
            if (auto_light_service_consume_fallback_notice(&auto_light_service, sensor_reason, sizeof(sensor_reason)))
            {
                memset(response_buffer, 0, sizeof(response_buffer));
                snprintf(response_buffer,
                         sizeof(response_buffer),
                         "EVENT,SENSOR_ERROR,OK,FALLBACK_HOLD_LAST_OUTPUT_%s,%lu",
                         sensor_reason,
                         (unsigned long)to_ms_since_boot(get_absolute_time()));
                broadcast_event(response_buffer);
            }
            if (auto_light_service_consume_recovery(&auto_light_service))
            {
                memset(response_buffer, 0, sizeof(response_buffer));
                snprintf(response_buffer,
                         sizeof(response_buffer),
                         "EVENT,SENSOR_ERROR,OK,SENSOR_RECOVERED,%lu",
                         (unsigned long)to_ms_since_boot(get_absolute_time()));
                broadcast_event(response_buffer);
            }
        }

        status_report_set_report_enabled(report_enabled);
        emit_uart_stat_events();

        if (report_enabled)
        {
            if (absolute_time_diff_us(last_report_time, get_absolute_time()) >= REPORT_INTERVAL_US)
            {
                print_status_report_uart(&mode_service, &light_service);
                print_status_report_usb(&mode_service, &light_service);
                last_report_time = get_absolute_time();
            }
        }

        uart_comm_task();
        service_watchdog();
        sleep_ms(MAIN_LOOP_DELAY_MS);
    }

    return 0;
}
