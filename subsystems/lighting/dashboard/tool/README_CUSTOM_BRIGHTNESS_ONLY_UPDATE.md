# 自定義模式調整紀錄

本版將自定義模式簡化為「固定亮度 + 顏色 + 燈效」。

## 已移除
- 自定義模式中的目標照度補光選項
- 自定義目標亮度 / 容許範圍 / 最低輸出 / 最高輸出 UI
- Dashboard bridge 的自定義 set_custom 發送路徑

## 保留
- 亮度 slider 0% ~ 100%
- 色彩模式：色溫 / RGB
- 色溫 slider
- RGB 調色盤與自選顏色
- RGB 紅 / 綠 / 藍 slider
- 燈效：關閉 / 靜態 / 呼吸 / 流水

## MQTT 行為
- 靜態 / RGB / 色溫：送 set_static
- 流水：送 set_flow
- 呼吸：先送 set_static，再送 set_static_breathing

本次未修改 MCU、Gateway C、driver，也未修改 MQTT topic。
