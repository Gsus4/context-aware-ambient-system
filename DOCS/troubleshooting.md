# 問題排查紀錄

## 1. LED 沒有反應

| 檢查項目 | 說明 |
|---|---|
| 供電 | 確認 LED 5V 與 GND 是否正確連接 |
| 接線 | 確認 DIN 是否接到 Pico W 指定 GPIO |
| 共地 | Raspberry Pi、Pico W、LED 是否共地 |
| 程式 | MCU 是否正確燒錄並執行 |
| 指令 | MQTT 指令是否有送達 Gateway |
| 回報 | 是否有 ACK / status 回傳 |

## 2. Dashboard 按鈕按下後沒有反應

建議排查順序：

1. 確認 Dashboard server 是否啟動。
2. 確認 MQTT Broker 是否啟動。
3. 使用 `mosquitto_sub` 監看 cmd / ack / status topic。
4. 確認 Gateway 是否收到指令。
5. 確認 MCU 是否收到 UART 指令。
6. 確認 LED 供電與接線。

## 3. 狀態不同步

可能原因：

- 指令已送出，但 ACK 或 status 尚未回傳。
- status 輪詢頻率過高或過低。
- 使用者連續操作造成狀態更新順序混亂。
- Gateway 與 MCU 對模式欄位的理解不一致。

處理方向：

- 操作後先更新 UI 狀態，避免使用者感覺卡頓。
- 降低不必要的 status 查詢頻率。
- 保留最後一次操作狀態。
- 統一對外 status 欄位格式。

## 4. 面試可用說法

> 我在專題中不是只把功能做出來，也有處理通訊整合與穩定性問題。例如 Dashboard 快速切換時可能產生重複請求與狀態不同步，所以我會從 MQTT topic、Gateway log、MCU 回覆與 UI 狀態逐層排查，確認問題發生在哪一層，再決定是要調整前端操作節奏、Gateway 狀態回報，還是 MCU 控制邏輯。
