#include <stddef.h>
// 引入環境光服務標頭檔
#include "light_sensor_service.h"

// 定義初始化函式
bool light_sensor_service_init(light_sensor_service_t *service, bh1750_t *sensor)
{
    // 如果 service 為空
    if (service == NULL)
    {
        // 回傳失敗
        return false;
    }

    // 如果 sensor 為空
    if (sensor == NULL)
    {
        // 回傳失敗
        return false;
    }

    // 保存感測器指標
    service->sensor = sensor;

    // 初始化最近一次 lux 值
    service->last_lux = 0.0f;

    // 設定目前尚無有效讀值
    service->has_valid_lux = false;

    // 初始化底層感測器
    return bh1750_init(service->sensor);
}

// 定義讀取 lux 函式
bool light_sensor_service_read_lux(light_sensor_service_t *service, float *lux_value)
{
    // 如果 service 為空
    if (service == NULL)
    {
        // 回傳失敗
        return false;
    }

    // 如果 sensor 為空
    if (service->sensor == NULL)
    {
        // 回傳失敗
        return false;
    }

    // 如果 lux_value 為空
    if (lux_value == NULL)
    {
        // 回傳失敗
        return false;
    }

    // 呼叫底層驅動讀值
    bool result = bh1750_read_lux(service->sensor, lux_value);

    // 如果成功
    if (result)
    {
        // 更新最近一次 lux
        service->last_lux = *lux_value;

        // 標記為有效
        service->has_valid_lux = true;
    }

    // 回傳結果
    return result;
}

// 定義取得最近一次 lux 函式
bool light_sensor_service_get_last_lux(const light_sensor_service_t *service, float *lux_value)
{
    // 如果 service 為空
    if (service == NULL)
    {
        // 回傳失敗
        return false;
    }

    // 如果 lux_value 為空
    if (lux_value == NULL)
    {
        // 回傳失敗
        return false;
    }

    // 如果目前沒有有效 lux
    if (!service->has_valid_lux)
    {
        // 回傳失敗
        return false;
    }

    // 輸出最近一次 lux
    *lux_value = service->last_lux;

    // 回傳成功
    return true;
}