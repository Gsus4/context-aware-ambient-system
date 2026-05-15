# UART / MCU 通訊說明

## 用途

UART 用於 Raspberry Pi Gateway 與 Pico W MCU 之間的控制與狀態交換。上層 Dashboard / Server 發出的 MQTT 指令，會先由 Raspberry Pi Gateway 接收，再轉換為 MCU 可處理的控制資料。

## 角色分工

| 元件 | 職責 |
|---|---|
| Raspberry Pi Gateway | 接收 MQTT、轉發控制指令、整理 MCU 回覆、發布 status / ack |
| Pico W MCU | 解析控制命令、執行 LED 控制、讀取感測器、回傳狀態 |

## 面試說明重點

可以用以下方式說明：

> MQTT 負責上層系統到 Node 的網路通訊，UART 負責 Raspberry Pi 與 Pico W 之間的本地硬體通訊。這樣可以讓 Dashboard 不需要直接控制 MCU，而是透過 Gateway 做橋接與狀態整理。

## 待補資料

- 完整 UART frame 格式：[待補資料]
- MCU 回傳格式範例：[待補資料]
- 錯誤碼完整對照表：[待補資料]
