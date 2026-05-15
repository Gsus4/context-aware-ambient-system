// 防止標頭檔重複引用
#ifndef LOGGER_H
// 防止標頭檔重複引用
#define LOGGER_H

// 宣告資訊訊息輸出函式
void log_info(const char *message);

// 宣告錯誤訊息輸出函式
void log_error(const char *message);

// 結束防重複引用區塊
#endif