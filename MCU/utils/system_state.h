#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

/*
 * 系統狀態機
 * --------------------------------------------------
 * 與燈光模式不同，這裡描述的是整體系統生命週期
 */
typedef enum
{
    SYSTEM_STATE_INIT = 0,   // 初始化中
    SYSTEM_STATE_READY,      // 初始化完成，準備就緒
    SYSTEM_STATE_RUNNING,    // 正常運行中
    SYSTEM_STATE_ERROR       // 發生錯誤
} system_state_t;

/*
 * 將系統狀態轉成字串
 */
static inline const char *system_state_to_string(system_state_t state)
{
    switch (state)
    {
        case SYSTEM_STATE_INIT:    return "INIT";
        case SYSTEM_STATE_READY:   return "READY";
        case SYSTEM_STATE_RUNNING: return "RUNNING";
        case SYSTEM_STATE_ERROR:   return "ERROR";
        default:                   return "UNKNOWN";
    }
}

#endif