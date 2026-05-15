// 防止標頭檔重複引用
#ifndef BH1750_H
// 防止標頭檔重複引用
#define BH1750_H

// 引入布林型別
#include <stdbool.h>

// 引入 Pico SDK 標準函式庫
#include "pico/stdlib.h"

// 引入 I2C 硬體函式庫
#include "hardware/i2c.h"

// 定義 BH1750 裝置結構
typedef struct
{
    // 紀錄使用的 I2C 埠
    i2c_inst_t *i2c_port;
    // 紀錄裝置位址
    uint8_t address;
} bh1750_t;

// 宣告初始化函式
bool bh1750_init(bh1750_t *device);

// 宣告檢查裝置是否存在函式
bool bh1750_is_available(bh1750_t *device);

// 宣告讀取 lux 函式
bool bh1750_read_lux(bh1750_t *device, float *lux_value);

// 結束防重複引用區塊
#endif