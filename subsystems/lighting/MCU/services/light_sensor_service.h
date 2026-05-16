// 防止標頭檔重複引用
#ifndef LIGHT_SENSOR_SERVICE_H
// 防止標頭檔重複引用
#define LIGHT_SENSOR_SERVICE_H

// 引入布林型別
#include <stdbool.h>

// 引入 BH1750 標頭檔
#include "../drivers/bh1750.h"

// 定義環境光服務結構
typedef struct
{
    // 保存感測器指標
    bh1750_t *sensor;

    // 保存最近一次 lux
    float last_lux;

    // 紀錄最近一次讀值是否有效
    bool has_valid_lux;

} light_sensor_service_t;

// 宣告初始化函式
bool light_sensor_service_init(light_sensor_service_t *service, bh1750_t *sensor);

// 宣告讀取 lux 函式
bool light_sensor_service_read_lux(light_sensor_service_t *service, float *lux_value);

// 宣告取得最近一次 lux 函式
bool light_sensor_service_get_last_lux(const light_sensor_service_t *service, float *lux_value);

// 結束防重複引用區塊
#endif