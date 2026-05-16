# 專題面試摘要

## 30 秒版本

本專題是智慧照明控制子系統，透過 Dashboard、MQTT、Raspberry Pi、Pico W 與 LED 燈條完成情境照明控制。系統支援情境模式、自定義模式、亮度與色溫控制，並能回報 ACK、status、event 與 availability。我主要負責通訊整合、LED 控制流程、感測器整合、測試驗證與問題排查。

## 3 分鐘版本

我的專題是「智慧情境感知與空間氛圍控制系統」中的照明控制子系統，主要目標是讓燈光可以依照使用情境與環境狀態進行控制。

整體架構分成 Dashboard、MQTT、Raspberry Pi Gateway、Pico W MCU、LED 燈條與感測器。Dashboard 或 Server 會透過 MQTT 發送控制指令到 Raspberry Pi，Raspberry Pi 上的 Gateway 接收指令後，再透過 UART 或 driver 傳給 Pico W。Pico W 負責實際控制 LED 與讀取感測器，最後再將狀態回傳給 Gateway，由 Gateway 發布 MQTT status、ack、event 與 availability 給上層系統。

我負責的部分包含 Raspberry Pi 與 Pico W 之間的通訊整合、MQTT 與 UART 指令格式整理、LED 控制邏輯、感測器整合、測試腳本操作與 log 排查。系統功能包含五種情境模式、自定義亮度與色溫控制、Dashboard 遠端操作、狀態回報與通訊驗證。

專題過程中遇到過狀態不同步、通訊等待逾時、回報格式不一致與快速操作造成卡頓等問題。我會從 Dashboard、MQTT topic、Gateway log、MCU 回覆與 LED 實際反應逐層排查，確認問題發生在哪一層，再調整通訊流程、狀態回報或操作節奏。

這個專題讓我學到嵌入式系統不只是把單一功能做出來，也需要注意系統分層、通訊格式、錯誤處理、測試驗證與交接文件，這些能力也對 IoT、嵌入式、FAE 或系統整合職務有幫助。

## 常見面試問題準備

| 問題 | 回答方向 |
|---|---|
| 你負責哪一部分？ | 通訊整合、LED 控制、感測器整合、測試驗證、問題排查 |
| 為什麼用 MQTT？ | 適合跨裝置 publish / subscribe，上層系統與 Node 可解耦 |
| Raspberry Pi 與 Pico W 分別做什麼？ | Raspberry Pi 做 Gateway 與通訊橋接，Pico W 做硬體控制 |
| LED 沒反應怎麼查？ | 供電、共地、DIN、GPIO、MQTT 指令、Gateway log、MCU 回覆 |
| 專題最大的收穫？ | 學到系統分層、通訊格式、測試驗證與錯誤排查流程 |
