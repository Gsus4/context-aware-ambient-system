# Environment Subsystem

## 中文簡介

這個子系統負責空間中的「環境狀態」與「風扇執行」兩件事。從 repo 結構來看，它拆成：

- `sensing/`：蒐集溫度、濕度、空氣品質與 PIR 相關資訊
- `fan-control/`：接收命令、執行風扇控制、回報狀態

它在整體系統中的角色，是提供主控端做情境判斷與自動控制所需的空間條件。

## English Summary

This subsystem handles environmental sensing and fan control. It provides room-level context such as temperature, humidity, air quality, and occupancy-related signals, while also exposing fan control and status reporting through MQTT-connected runtime components.

## Structure

```text
sensing/
  picow/        Pico W side sensor collection
  rpi/app/      Raspberry Pi side application

fan-control/
  app/          Fan control runtime
  driver_versions/ Linux driver-related code
  Pico_W/       MCU-side assets
```

## What The Code Suggests

從現有檔案可以安全看出這些重點：

- 感測端包含 Pico W 與 Raspberry Pi 兩側實作
- 風扇控制透過 MQTT topic 接收命令與回傳狀態
- 風扇控制流程也整合了 UART 與 controller heartbeat
- repo 中保留 Linux driver 版本資料，表示當時有處理硬體層整合

## Role In The Full System

在完整專案裡，這個子系統把「空間目前環境如何」與「風扇目前怎麼運作」傳回主控系統，再由主控決定是否切換 scene 或同步更新 Dashboard。
