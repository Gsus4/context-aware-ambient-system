#include "error_code.h"

/*
 * 將錯誤碼轉為固定 ID
 */
const char *error_code_to_id(error_code_t code)
{
    switch (code)
    {
        case ERROR_CODE_NONE:             return "E000";
        case ERROR_CODE_INVALID_ARGC:     return "E001";
        case ERROR_CODE_INVALID_VALUE:    return "E002";
        case ERROR_CODE_INTERNAL:         return "E003";
        case ERROR_CODE_UNKNOWN_COMMAND:  return "E004";
        case ERROR_CODE_BUFFER_OVERFLOW:  return "E005";
        case ERROR_CODE_NO_DATA:          return "E006";

        case ERROR_CODE_SENSOR_INIT_FAIL: return "E101";
        case ERROR_CODE_LED_INIT_FAIL:    return "E102";
        case ERROR_CODE_UART_INIT_FAIL:   return "E103";
        case ERROR_CODE_MODE_UPDATE_FAIL: return "E104";

        default:                          return "E999";
    }
}

/*
 * 將錯誤碼轉為文字描述
 */
const char *error_code_to_string(error_code_t code)
{
    switch (code)
    {
        case ERROR_CODE_NONE:             return "NONE";
        case ERROR_CODE_INVALID_ARGC:     return "INVALID_ARGC";
        case ERROR_CODE_INVALID_VALUE:    return "INVALID_VALUE";
        case ERROR_CODE_INTERNAL:         return "INTERNAL";
        case ERROR_CODE_UNKNOWN_COMMAND:  return "UNKNOWN_COMMAND";
        case ERROR_CODE_BUFFER_OVERFLOW:  return "BUFFER_OVERFLOW";
        case ERROR_CODE_NO_DATA:          return "NO_DATA";

        case ERROR_CODE_SENSOR_INIT_FAIL: return "SENSOR_INIT_FAIL";
        case ERROR_CODE_LED_INIT_FAIL:    return "LED_INIT_FAIL";
        case ERROR_CODE_UART_INIT_FAIL:   return "UART_INIT_FAIL";
        case ERROR_CODE_MODE_UPDATE_FAIL: return "MODE_UPDATE_FAIL";

        default:                          return "UNKNOWN_ERROR";
    }
}