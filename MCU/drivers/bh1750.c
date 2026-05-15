// 引入 BH1750 標頭檔
#include "bh1750.h"

// 定義連續高解析模式指令
#define BH1750_POWER_ON 0x01
#define BH1750_CONTINUOUS_HIGH_RES_MODE 0x10

// 定義初始化 BH1750 函式
bool bh1750_init(bh1750_t *device)
{
    // 定義要寫入的模式指令
    uint8_t command = BH1750_CONTINUOUS_HIGH_RES_MODE;

    // 將模式指令寫入感測器
    int result = i2c_write_blocking(device->i2c_port, device->address, &command, 1, false);

    // 如果寫入結果不等於 1
    if (result != 1)
    {
        // 回傳失敗
        return false;
    }

    // 等待感測器進入量測模式
    sleep_ms(200);

    // 回傳成功
    return true;
}

// 定義檢查裝置是否存在函式
bool bh1750_is_available(bh1750_t *device)
{
    // 定義一個較低副作用的喚醒指令
    uint8_t command = BH1750_POWER_ON;

    // 嘗試寫入感測器
    int result = i2c_write_blocking(device->i2c_port, device->address, &command, 1, false);

    // 如果結果小於 0
    if (result < 0)
    {
        // 回傳不存在
        return false;
    }

    // 回傳存在
    return true;
}

// 定義讀取 lux 函式
bool bh1750_read_lux(bh1750_t *device, float *lux_value)
{
    // 定義接收用的 2-byte 緩衝區
    uint8_t buffer[2] = {0};

    // 從感測器讀取 2 個位元組
    int result = i2c_read_blocking(device->i2c_port, device->address, buffer, 2, false);

    // 如果讀取結果不等於 2
    if (result != 2)
    {
        // 回傳失敗
        return false;
    }

    // 將兩個位元組組成 16-bit 原始值
    uint16_t raw_value = ((uint16_t)buffer[0] << 8) | buffer[1];

    // 將原始值換算成 lux
    *lux_value = raw_value / 1.2f;

    // 回傳成功
    return true;
}