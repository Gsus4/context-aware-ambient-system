# Wearable Subsystem

## 中文簡介

這個子系統負責提供「人體狀態」資料，是整個情境感知系統中的個人感測來源。從 repo 內容可辨識的核心組成包括：

- MAX30102：心率 / 血氧
- MPU6050：動作感測
- SSD1306：顯示輸出
- Pico W：Wi-Fi 連線
- FreeRTOS：多 task 架構

它讓整個專案不只依靠環境資料判斷情境，也能把人的生理與活動狀態納入系統。

## English Summary

This subsystem is the personal sensing layer of the project. It appears to combine heart-rate and SpO2 sensing, motion sensing, local display output, Wi-Fi connectivity, and FreeRTOS-based task orchestration on Pico W hardware.

## Structure

```text
mcu/
  main.c
  MAX30102.*
  MPU6050.*
  SSD1306.*
  battery.*
  time_sync.*
  FreeRTOS-Kernel/
```

## What It Contributes

- 心率與血氧資料
- 活動相關狀態推估
- 本地裝置顯示
- 透過網路對外傳送感測結果

## Role In The Full System

在整體架構中，wearable data 會補足環境感測看不到的個人層訊號，使主控端在推斷 context 時，能同時參考空間狀態與人的身體狀態。
