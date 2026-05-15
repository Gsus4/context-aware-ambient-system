# SmartLight IoT Lighting Control System

## 專案簡介

本專案為「智慧情境感知與空間氛圍控制系統」中的照明氛圍控制子系統，主要透過 Dashboard、MQTT、Raspberry Pi、Pico W、UART 與 LED 燈條，完成室內照明的情境控制、自定義亮度 / 色溫調整、狀態回報與裝置控制功能。

本專案目標是讓使用者能透過 Dashboard 操作照明情境，並由 Raspberry Pi 作為 Node / Gateway 進行指令橋接，再由 Pico W 負責 LED 燈條控制與感測資料整合。

## 使用技術

- C 語言
- Raspberry Pi
- Pico W
- MQTT
- UART
- I2C
- GPIO
- LED 燈條控制
- 感測器整合
- Linux 基礎操作
- JavaScript / HTML / CSS 基礎
- VS Code
- CMake
- Ninja
- log 紀錄與通訊測試

---

## 主要功能

- Dashboard 遠端照明控制
- 情境模式切換
- 自定義亮度與色溫控制
- MQTT 指令傳送
- UART 裝置通訊
- Pico W LED 控制
- 感測器資料整合
- 裝置狀態回報
- 通訊測試與錯誤排查


## 資料夾說明

```text
DOCS/       專案文件、規格說明與交接資料
MCU/        Pico W / MCU 端程式
NODE/       Raspberry Pi Node / Gateway 相關程式
dashboard/  Dashboard 控制介面與前端相關檔案
```

