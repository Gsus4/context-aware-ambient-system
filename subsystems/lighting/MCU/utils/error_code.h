#ifndef ERROR_CODE_H
#define ERROR_CODE_H

/*
 * 錯誤碼列舉
 * --------------------------------------------------
 * 用來統一管理系統中的錯誤類型
 */
typedef enum
{
    // 無錯誤
    ERROR_CODE_NONE = 0,

    // 通用錯誤
    ERROR_CODE_INVALID_ARGC,       // 參數數量錯誤
    ERROR_CODE_INVALID_VALUE,      // 參數值錯誤
    ERROR_CODE_INTERNAL,           // 內部錯誤
    ERROR_CODE_UNKNOWN_COMMAND,    // 未知命令
    ERROR_CODE_BUFFER_OVERFLOW,    // 緩衝區溢位
    ERROR_CODE_NO_DATA,            // 尚無可用資料

    // 系統 / 硬體錯誤
    ERROR_CODE_SENSOR_INIT_FAIL,   // 感測器初始化失敗
    ERROR_CODE_LED_INIT_FAIL,      // LED 初始化失敗
    ERROR_CODE_UART_INIT_FAIL,     // UART 初始化失敗
    ERROR_CODE_MODE_UPDATE_FAIL    // 模式更新失敗

} error_code_t;

/*
 * 將錯誤碼轉成固定錯誤代號
 * 範例：E001
 */
const char *error_code_to_id(error_code_t code);

/*
 * 將錯誤碼轉成文字描述
 * 範例：INVALID_ARGC
 */
const char *error_code_to_string(error_code_t code);

#endif