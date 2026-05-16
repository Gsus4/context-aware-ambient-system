#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include <math.h>
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "MPU6050.h"
#include "MAX30102.h"
#include "SSD1306.h"
#include "global_defines.h"
#include "MQTT.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "bettery.h"
#include "lwip/apps/sntp.h"
#include <time.h>
#include "pico/cyw43_arch.h"

#define STATE_UNKNOWN    -1
#define STATE_REST       1
#define STATE_ACTIVE     2
#define STATE_HIGH_LOAD  3

#define Main_DEBUG 0

#if Main_DEBUG
#define DBG(...) printf(__VA_ARGS__)
#else
#define DBG(...)
#endif

QueueHandle_t sensorQueue;
QueueHandle_t batteryQueue;
QueueHandle_t mqttQueue;
QueueHandle_t sampleQueue;
QueueHandle_t stateQueue;
SemaphoreHandle_t i2cMutex;


typedef struct {
    uint32_t red;
    uint32_t ir;
    int bpm;
    int spo2;
    int state;
    int hr_trend;
    int steps;
    float activity;
    float battery_v;   
    int   battery_p;
    int has_signal;
} sensor_data_t;

typedef struct {
    float v;
    int p;
} battery_data_t;

typedef struct {
    uint32_t red;
    uint32_t ir;
    uint32_t t_ms;
} sample_t;

typedef enum {
    HR_STABLE = 0,
    HR_RISING,
    HR_FALLING
} hr_trend_t;

typedef struct {
    int state;
    float hr_avg;
    float act_avg;
    int has_signal;
} state_data_t;

void DisplayTask(void *param);
void NetworkTask(void *param);
void ReadTask(void *param);
void ProcessTask(void *param);
void BatteryTask(void *param);
void StateTask(void *param);
hr_trend_t calc_hr_trend(int current, int prev);
int classify_state(float act, int has_signal);
void time_sync_init(void);
void wait_time_sync(void);

    int main() {

        stdio_init_all();
        sleep_ms(500);

        printf("Start RTOS System\n");

        // I2C init
        i2c_init(I2C0_PORT, 400 * 1000);
        gpio_set_function(I2C0_SDA, GPIO_FUNC_I2C);
        gpio_set_function(I2C0_SCL, GPIO_FUNC_I2C);
        gpio_pull_up(I2C0_SDA);
        gpio_pull_up(I2C0_SCL);
        
        //解決資源互搶問題
        i2cMutex = xSemaphoreCreateMutex();

        if (cyw43_arch_init()) {
            printf("WiFi init failed\n");
            while (1);
        }

        cyw43_arch_enable_sta_mode();

        printf("Connecting WiFi...\n");

        if (cyw43_arch_wifi_connect_timeout_ms(
                WIFI_SSID, WIFI_PASS,
                CYW43_AUTH_WPA2_AES_PSK, 30000)) {
            printf("WiFi connect failed\n");
        } else {
            printf("WiFi connected\n");
            while (netif_default == NULL ||
                netif_default->ip_addr.addr == 0) {
                sleep_ms(1000);
        }
        ip_addr_t ntp_server;
        IP4_ADDR(&ntp_server, 216, 239, 35, 0); // Google NTP

        sntp_setserver(0, &ntp_server);
        sntp_init();
        printf("TIME(before NTP) = %d\n", (int)time(NULL));
        wait_time_sync();
        printf("TIME(after NTP) = %d\n", (int)time(NULL));
        sleep_ms(2000);
        mqtt_init();
        }


        max30102_init();
        max30102_setup();
        max30102_hr_init();
        max30102_spo2_init();

        SSD1306_init();
        SSD1306_clear();

        mpu6050_init();
        mpu6050_calibrate();


        printf("ALL INIT DONE\n");

        // 建 queue
        sensorQueue = xQueueCreate(1, sizeof(sensor_data_t));
        batteryQueue = xQueueCreate(1, sizeof(battery_data_t));
        mqttQueue = xQueueCreate(1, sizeof(sensor_data_t));
        stateQueue = xQueueCreate(1, sizeof(state_data_t));
        sampleQueue = xQueueCreate(256, sizeof(sample_t));

        // 建 task
        xTaskCreate(ReadTask,    "read",    1024, NULL, 2, NULL);
        xTaskCreate(ProcessTask, "proc",    2048, NULL, 3, NULL);
        xTaskCreate(NetworkTask, "NET",     2048, NULL, 1, NULL);
        xTaskCreate(DisplayTask, "Display", 512, NULL, 1, NULL);
        xTaskCreate(BatteryTask, "BAT", 1024, NULL, 1, NULL);
        xTaskCreate(StateTask, "STATE", 1024, NULL, 2, NULL);

        // 啟動 RTOS
        vTaskStartScheduler();

        while (1){
            printf("[MAIN ALIVE]\n");
            sleep_ms(2000);
        }
    }


void DisplayTask(void *param)
{
    sensor_data_t data;
    int bpm = 0, spo2 = 0;
    int trend = 0;
    int state = 0;
    int steps = 0;
    int battery = 0;

    while (1)
    {
        if (xQueuePeek(sensorQueue, &data, 0)) {
            bpm   = data.bpm;
            spo2  = data.spo2;
            trend = data.hr_trend;
            state = data.state;    
            steps = data.steps; 
            battery = data.battery_p;
        }

        xSemaphoreTake(i2cMutex, portMAX_DELAY);

        display_update(bpm, spo2, state, trend, steps, battery);

        xSemaphoreGive(i2cMutex);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void NetworkTask(void *param) {

    printf("NetworkTask start\n");

    sensor_data_t data;
    static sensor_data_t last_data = {0};
    state_data_t sdata;

    // ===== summary cache =====
    static float last_hr_avg = 0;
    static float last_act_avg = 0;
    static int   last_state30 = STATE_REST;

    // ===== timing =====
    uint32_t last_send_status = 0;
    uint32_t last_send_avail  = 0;
    uint32_t last_reconnect   = 0;

    while (1) {

        static uint32_t last_dbg = 0;

        cyw43_arch_poll();

        uint32_t now = to_ms_since_boot(get_absolute_time());

        // =========================
        // MQTT reconnect（每 5 秒）
        // =========================
        if (!mqtt_is_ready() && mqtt_get_client() == NULL) {
            if (now - last_reconnect > 60000) {
                printf("[MQTT] reconnecting...\n");
                mqtt_init();
                last_reconnect = now;
            }
        }

        // =========================
        // data flow
        // =========================
        if (xQueueReceive(mqttQueue, &data, 0)) {
            last_data = data;
        }

        if (xQueueReceive(stateQueue, &sdata, 0)) {
            last_hr_avg  = sdata.hr_avg;
            last_act_avg = sdata.act_avg;
            last_state30 = sdata.state;
        }

        // =========================
        // 每 5 秒送 status
        // =========================
        if (now - last_send_status > 5000) {

            if (mqtt_is_ready()) {
                mqtt_publish_status(
                    last_data.bpm,
                    last_data.spo2,
                    last_data.state,
                    last_data.hr_trend,
                    last_data.steps,
                    last_data.battery_p,
                    last_act_avg,
                    last_state30
                );
            }

            last_send_status = now;
        }

        // =========================
        // availability（每 10 秒）
        // =========================
        if (now - last_send_avail > 10000) {
            if (mqtt_is_ready())
                mqtt_publish_availability(1);
            last_send_avail = now;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void ReadTask(void *param)
{
    sample_t s;

    TickType_t lastWake = xTaskGetTickCount();

    #if Main_DEBUG
        static uint32_t last_t = 0;
    #endif

    while (1)
    {

        #if Main_DEBUG
            uint32_t t = time_us_64();
            if (last_t != 0) {
                DBG("[READ] dt=%u us\n", t - last_t);
            }
            last_t = t;
        #endif

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10)); // 100Hz

        xSemaphoreTake(i2cMutex, portMAX_DELAY);

        if (max30102_read_fifo(&s.red, &s.ir))
        {
            s.t_ms = to_ms_since_boot(get_absolute_time());

            // 保證不丟資料
            xQueueSend(sampleQueue, &s, 0);
        }

        xSemaphoreGive(i2cMutex);
    }
}

void ProcessTask(void *param)
{
    sample_t s;
    sensor_data_t data = {0};
    battery_data_t bat;

    TickType_t lastWake = xTaskGetTickCount();

    static int prev_bpm = -1;
    static int current_trend = HR_STABLE;

    while (1)
    {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10));

        // ===== IMU =====
        xSemaphoreTake(i2cMutex, portMAX_DELAY);
        mpu6050_update();
        xSemaphoreGive(i2cMutex);

        mpu6050_activity_update();
        mpu6050_step_counter_update();

        data.state    = mpu6050_get_state();
        data.steps    = mpu6050_get_steps();
        data.activity = mpu6050_get_activity();

        // ===== HR（改回 do-while drain queue）=====
        if (xQueueReceive(sampleQueue, &s, 0))
        {
            do {
                max30102_hr_update(s.red, s.ir, s.t_ms);
                max30102_spo2_update(s.red, s.ir);

                int bpm  = max30102_get_bpm();
                int spo2 = max30102_get_spo2();

                if (!max30102_has_signal()) {
                    prev_bpm = -1;
                    current_trend = HR_STABLE;
                } else {
                    if (prev_bpm == -1) prev_bpm = bpm;

                    if (bpm != prev_bpm)
                        current_trend = calc_hr_trend(bpm, prev_bpm);

                    prev_bpm = bpm;
                }

                data.red        = s.red;
                data.ir         = s.ir;
                data.bpm        = bpm;
                data.spo2       = spo2;
                data.hr_trend   = current_trend;
                data.has_signal = max30102_has_signal();

            } while (
                uxQueueMessagesWaiting(sampleQueue) > 0 &&
                xQueueReceive(sampleQueue, &s, 0)
            );
        }

        // ===== battery =====
        static battery_data_t last_bat = {0};

        if (xQueueReceive(batteryQueue, &bat, 0)) {
            last_bat = bat;
        }

        data.battery_v = last_bat.v;
        data.battery_p = last_bat.p;

        // ===== output =====
        xQueueOverwrite(sensorQueue, &data);
        xQueueOverwrite(mqttQueue, &data);
    }
}

void BatteryTask(void *param) {

    vTaskDelay(pdMS_TO_TICKS(10000));
    
    float v;
    int p;

    battery_data_t bat;

    battery_init();

    while (1) {

        battery_update(&v, &p);

        bat.v = v;
        bat.p = p;

        xQueueOverwrite(batteryQueue, &bat);

/*         printf("[BAT] %.2f V (%d%%)\n", v, p); */

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void StateTask(void *param)
{
    sensor_data_t data;
    state_data_t state_data;

    static float act_sum = 0;
    static int count = 0;

    static int current_state = STATE_REST;
    static uint32_t start_time = 0;
    static uint32_t last_sample_time = 0;

    while (1)
    {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        if (start_time == 0)
            start_time = now;

        //  10Hz sampling（避免重複累加同一筆）
        if (now - last_sample_time >= 100)
        {
            if (xQueuePeek(sensorQueue, &data, 0))
            {
                if (data.bpm > 40 && data.bpm < 180)
                {
                    act_sum += data.activity;
                    count++;
                }
            }
            last_sample_time = now;
        }

        //  30秒統計
        if (now - start_time >= 30000)
        {
            if (count > 0)
            {
                state_data.act_avg = act_sum / count;

                current_state = classify_state(
                    state_data.act_avg,
                    data.has_signal
                );

                state_data.state = current_state;

                xQueueOverwrite(stateQueue, &state_data);
            }

            // reset
            act_sum = 0;
            count = 0;
            start_time = now;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    printf("Stack overflow: %s\n", pcTaskName);
    while (1);
}

void vApplicationMallocFailedHook(void) {
    printf("Malloc failed\n");
    while (1);
}

hr_trend_t calc_hr_trend(int current, int prev) {
    int diff = current - prev;

    if (diff > 0) return HR_RISING;     
    if (diff < 0) return HR_FALLING;
    return HR_STABLE;
}

int classify_state(float act, int has_signal)
{
    if (!has_signal){
        return STATE_UNKNOWN;  // 或 UNKNOWN
    }

    int score = 0;

    // ACT 貢獻
    if (act > 0.20f) score = 2;
    else if (act > 0.15f) score = 1;

    // 決策
    if (score == 0)
        return STATE_REST;
    else if (score == 1)
        return STATE_ACTIVE;
    else
        return STATE_HIGH_LOAD;
}

void time_sync_init() {
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

void wait_time_sync() {
    while (time(NULL) < 1700000000) {
        cyw43_arch_poll(); 
        printf("Waiting NTP...\n");
        sleep_ms(1000);
    }
    printf("Time synced: %d\n", (int)time(NULL));
}
