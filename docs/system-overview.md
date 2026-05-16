# System Overview

## 專案定位

`Context-Aware Ambient System` 是一個整合型智慧空間 side project。它的核心想法不是單獨控制某個裝置，而是讓空間可以根據「人是否在場、人在做什麼、環境狀況如何、身體訊號如何」來調整輸出。

英文一句話版本：
An integrated ambient computing project where a central controller coordinates lighting, fan behavior, music, and dashboard feedback using sensor streams, wearable data, and scene awareness.

## 系統目標

這個系統希望完成三件事：

1. 收集空間上下文
來源包含環境感測、PIR、穿戴式資料與相機畫面。

2. 建立共享的情境理解
主控端把不同來源整理成統一狀態，避免每個模組各自理解世界。

3. 驅動實際空間回應
依照情境變化輸出燈光、風扇、音樂與 Dashboard 更新。

## High-Level Architecture

```text
                           +----------------------+
                           |  Browser Dashboard   |
                           |  HTTP / WebSocket    |
                           +----------+-----------+
                                      |
                                      v
                           +----------------------+
                           | Central Controller   |
                           | Raspberry Pi         |
                           | FastAPI + Runtime    |
                           +----------+-----------+
                                      |
                     +----------------+----------------+
                     |                |                |
                     v                v                v
            +---------------+  +-------------+  +-------------+
            | Lighting      |  | Environment |  | Wearable    |
            | Node / MCU    |  | Sensing/Fan |  | Pico W MCU  |
            +---------------+  +-------------+  +-------------+
                     ^                ^                ^
                     +----------------+----------------+
                                      |
                                      v
                                  MQTT Broker
```

## 主要模組

### 1. Central Controller

位於 [central-controller/](/home/pi/context-aware-ambient-system/central-controller/README.md:1)。

這是整個系統的中樞，負責：

- 提供 Web Dashboard、REST API、WebSocket
- 維護全局 system state
- 訂閱多來源 MQTT 訊息
- 啟動 scene awareness、fusion control、music orchestration
- 將 context 轉成實際控制命令

它不是單純的 MQTT bridge，而是整合「觀測、狀態、決策、輸出」的控制核心。

### 2. Lighting Subsystem

位於 [subsystems/lighting/](/home/pi/context-aware-ambient-system/subsystems/lighting/README.md:1)。

這個子系統的角色是把來自主控或 Dashboard 的燈光控制要求，轉成 Raspberry Pi Gateway 與 Pico W MCU 可以執行的硬體控制流程。repo 內保留了：

- Dashboard 舊版前端資產
- Node / Gateway 與 MCU 結構
- MQTT 與 UART 規格文件

### 3. Environment Subsystem

位於 [subsystems/environment/](/home/pi/context-aware-ambient-system/subsystems/environment/README.md:1)。

這部分分成兩塊：

- `sensing/`：環境感測與 PIR 事件來源
- `fan-control/`：風扇控制邏輯與 MQTT / UART 整合

從 repo 內容可看出它同時涉及 Raspberry Pi app、Pico W sensor collection、以及 Linux driver 相關內容。

### 4. Wearable Subsystem

位於 [subsystems/wearable/](/home/pi/context-aware-ambient-system/subsystems/wearable/README.md:1)。

這個子系統以 Pico W + FreeRTOS 為基礎，從程式結構可以辨識出它整合了：

- MAX30102 心率 / 血氧感測
- MPU6050 動作感測
- SSD1306 顯示
- Wi-Fi / MQTT
- 多 task 併行處理

它提供的是「人體狀態」這一層的 context，而不是單純環境資料。

## Data Flow

### Context In

進入主控系統的 context 來源包含：

- 環境感測資料：溫度、濕度、空氣品質、PIR
- 穿戴式資料：心率、血氧、活動狀態
- 相機觀測：供 scene awareness 使用
- 手動控制：來自 Dashboard 的使用者操作

### State Unification

主控端將這些資訊整合到 `/api/system-state` 對應的共享狀態模型。這一步很重要，因為它讓：

- Dashboard 顯示有一致資料來源
- 自動控制不必直接讀取各個子系統原始 payload
- 後續擴充其他服務時可以站在同一份狀態之上

### Control Out

當情境變化後，系統會把結果輸出到不同裝置：

- 燈光：scene / brightness / color 等控制
- 風扇：mode / fan level
- 音樂：依情境自動播放或停止
- Dashboard：即時顯示最新系統狀態

## Context-Aware Logic

這個專案最有趣的部分，是它不只做「遙控」，還做了「判斷後再控制」。

例如：

- 當場景被推論為有人活動，系統可維持 occupied scene
- 當 AI 連續判定無人，系統才切到 `vacant`
- 當 GPIO 按鈕或 PIR 重新觸發，系統會從 `vacant` 回到 `relax`
- 風扇與音樂可以跟著 scene 與 sensor context 協調變化

這些行為讓它從「裝置控制專案」變成更接近「空間互動系統」。

## Engineering Themes

這個 repo 比較能代表的工程主題是：

- 分層與模組化設計
- 實體硬體與網路通訊整合
- context modeling
- 邊緣運算與本地控制
- 產品體驗導向的系統整合

## Reading Order

若你是第一次看這個 repo，建議閱讀順序如下：

1. [README.md](/home/pi/context-aware-ambient-system/README.md:1)
2. [central-controller/README.md](/home/pi/context-aware-ambient-system/central-controller/README.md:1)
3. [central-controller/docs/system-state.md](/home/pi/context-aware-ambient-system/central-controller/docs/system-state.md:1)
4. [central-controller/docs/fusion-control.md](/home/pi/context-aware-ambient-system/central-controller/docs/fusion-control.md:1)
5. `subsystems/` 底下各子系統 README
