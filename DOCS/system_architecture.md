# 系統架構說明

## 架構概念

本專案採用分層架構，將使用者介面、通訊橋接與硬體控制分開，降低單一模組修改對其他模組的影響。

```text
Dashboard / Server / App
        │
        │ MQTT
        ▼
Raspberry Pi Node / Gateway
        │
        │ UART / driver
        ▼
Pico W MCU
        │
        ├── LED Strip
        └── Light Sensor
```

## 各層職責

| 層級 | 元件 | 職責 |
|---|---|---|
| 操作層 | Dashboard / Server / App | 發送控制指令、顯示即時狀態 |
| 通訊層 | MQTT Broker | 提供 publish / subscribe 通訊通道 |
| Gateway 層 | Raspberry Pi Node / Gateway C | 接收 MQTT 指令、轉發給 MCU、回報 ACK / status |
| 控制層 | Pico W MCU | 解析指令、控制 LED、讀取感測器、回傳狀態 |
| 裝置層 | LED / Sensor | 實際輸出燈光與提供環境資料 |

## 設計重點

- 上層系統不直接操作硬體，而是透過 MQTT 指令控制 Node。
- Raspberry Pi 負責橋接與狀態回報，Pico W 專注在硬體控制。
- MCU 與 Gateway 分層後，未來更換 Dashboard 或 Server 時，不需要大幅修改 MCU 控制邏輯。
