// 引入標準輸入輸出函式庫
#include <stdio.h>

// 引入 logger 標頭檔
#include "logger.h"

// 定義資訊訊息輸出函式
void log_info(const char *message)
{
    // 輸出 INFO 前綴與訊息內容
    printf("[INFO] %s\n", message);
}

// 定義錯誤訊息輸出函式
void log_error(const char *message)
{
    // 輸出 ERROR 前綴與訊息內容
    printf("[ERROR] %s\n", message);
}