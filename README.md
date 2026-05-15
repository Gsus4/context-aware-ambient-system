# SmartLight IoT Lighting Control System

## 專案簡介

本專案為「智慧情境感知與空間氛圍控制系統」中的照明氛圍控制子系統，主要透過 Dashboard、MQTT、Raspberry Pi、Pico W、UART 與 LED 燈條，完成室內照明的情境控制、自定義亮度 / 色溫調整、狀態回報與裝置控制功能。

本專案目標是讓使用者能透過 Dashboard 操作照明情境，並由 Raspberry Pi 作為 Node / Gateway 進行指令橋接，再由 Pico W 負責 LED 燈條控制與感測資料整合。

## 我的負責項目

- Raspberry Pi 與 Pico W 通訊整合
- MQTT / UART 指令流程整理
- LED 控制邏輯實作與測試
- 感測器資料與照明控制流程整合
- 狀態回報流程整理
- log 紀錄、測試驗證與問題排查
- 專案文件與交接資料整理

---

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

---

## 專案成果

- 完成 Dashboard 至 Raspberry Pi，再由 Raspberry Pi 傳送至 Pico W 的控制流程
- 完成 LED 情境控制與自定義控制功能
- 建立 MQTT / UART 通訊流程
- 透過 log 與測試腳本進行功能驗證
- 針對通訊中斷、狀態回報不一致、等待逾時與控制不同步等問題進行分析與修正
- 強化系統穩定性、測試可讀性與後續交接維護性

---

## 資料夾說明

```text
DOCS/       專案文件、規格說明與交接資料
MCU/        Pico W / MCU 端程式
NODE/       Raspberry Pi Node / Gateway 相關程式
dashboard/  Dashboard 控制介面與前端相關檔案
```

---

## 專題說明

本子系統中，Dashboard 負責使用者操作，MQTT 負責控制指令傳送，Raspberry Pi 作為 Node / Gateway 負責指令橋接與資料處理，Pico W 則接收 UART 指令並控制 LED 燈條。系統支援情境模式與自定義模式，可依不同使用情境調整燈光效果，並透過狀態回報讓使用者確認目前裝置狀態。

實作過程中，我主要負責 Raspberry Pi 與 Pico W 通訊整合、MQTT / UART 指令流程整理、LED 控制邏輯、感測器資料整合、測試驗證與問題排查。透過此專題，我累積了 C 語言、Raspberry Pi、Pico W、MQTT、UART、I2C、GPIO、感測器整合、LED 控制與系統整合經驗。

---
