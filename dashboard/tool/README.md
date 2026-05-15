# Dashboard 多 Broker 實機資料整合版

本版用途：在同一個 WEB Dashboard 中接入不同 MQTT Broker IP 的實機資料來源。

## Broker IP 設定

| 子系統 | Broker IP | Port | Node ID | 用途 |
|---|---:|---:|---|---|
| 燈光 / SmartLight | 127.0.0.1 | 1883 | bedroom01 | 燈光狀態與控制 |
| 心律 / 血氧 | 127.0.0.1 | 1883 | pico_wearable01 | heart_rate、spo2、summary.state 實機資料 |
| 環境監測 | 127.0.0.1 | 1883 | rpi_env01 / rpi_env02 | 溫度、濕度、空氣品質、PIR、風扇 |

## 心律 / 血氧 payload 簡化說明

本版心律 / 血氧資料不要求平均心率欄位。Server 端目前主要讀取：

```text
status.heart_rate
status.spo2
status.summary.state
```

活動狀態自動控制只依照 `status.summary.state` 判斷；平均心率欄位不再作為必要輸入。

## 啟動方式

1. 解壓縮 ZIP。
2. 進入 `dashboard` 資料夾。
3. 執行 `START_DASHBOARD.cmd`。
4. 開啟瀏覽器：`http://localhost:3000`

## 主要 MQTT Topic

### 燈光

```text
smartlight/bedroom01/status
smartlight/bedroom01/ack
smartlight/bedroom01/availability
smartlight/bedroom01/event
smartlight/bedroom01/cmd
```

### 心律 / 血氧

```text
integration/smart/v1/wearable/pico_wearable01/status
integration/smart/v1/wearable/pico_wearable01/availability
```

### 環境監測 / 風扇

```text
integration/smart/v1/env/rpi_env01/status
integration/smart/v1/env/rpi_env01/event
integration/smart/v1/env/rpi_env01/availability
integration/smart/v1/env/rpi_env02/status
integration/smart/v1/env/rpi_env02/ack
integration/smart/v1/env/rpi_env02/availability
integration/smart/v1/env/rpi_env02/command
```

## 調整位置

若之後 IP 有變更，可修改：

```text
START_DASHBOARD.cmd
server/.env.example
server/dashboard-mqtt-bridge.js
js/config.js
```

正式執行時以 `START_DASHBOARD.cmd` 內的環境變數為優先。
