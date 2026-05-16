# Dashboard 虛擬驗證說明

本資料夾已加入 PC 端虛擬 SmartLight 裝置，可在沒有 Pico W / Gateway / LED 實體硬體時驗證 Dashboard 的 MQTT payload 與 UI 流程。

## 一、用途

可驗證：

- Dashboard 是否正確送出 `set_scene`、`power_off`、`set_static`、`set_flow`、`set_static_breathing`
- 自定義模式是否已不再送 `set_custom` / `target_lux_range`
- RGB 與色溫是否不會同時送出
- scene debounce 是否只送最後一筆
- ACK / status / event 是否能解除 UI 等待狀態

不可驗證：

- 實體 LED 顏色是否準確
- BH1750 感測器真實讀值
- Pico W UART / Gateway C 實體穩定度
- 實體電源與燈條狀況

## 二、啟動前需求

PC 需安裝：

- Node.js LTS
- Mosquitto Broker

若已安裝 Mosquitto，可先開啟本機 broker：

```cmd
net start mosquitto
```

若 `net start mosquitto` 需要管理員權限，請用系統管理員身分開啟 CMD。

## 三、最簡單啟動方式

雙擊：

```text
START_VIRTUAL_VALIDATION_ALL.cmd
```

它會開兩個視窗：

1. `Mock SmartLight`：虛擬 SmartLight 裝置
2. `Dashboard Virtual`：Dashboard Web

瀏覽器會開啟：

```text
http://localhost:3000
```

## 四、手動分開啟動

視窗 1：啟動虛擬 SmartLight：

```cmd
START_MOCK_SMARTLIGHT_LOCAL.cmd
```

視窗 2：啟動 Dashboard：

```cmd
START_DASHBOARD_VIRTUAL_LOCAL.cmd
```

視窗 3：監聽 MQTT：

```cmd
mosquitto_sub -h 127.0.0.1 -t smartlight/bedroom01/# -v
```

## 五、測試項目

### 1. 情境切換

Web 點選：

```text
無人 → 睡眠 → 放鬆 → 工作 → 運動
```

預期：

- MQTT 出現 `cmd:set_scene`
- ACK `ok:true`
- event 顯示 `scene_xxx_applied`
- status 更新 `active_scene`

### 2. 快速切換

快速點：

```text
睡眠 → 運動
```

預期：

- 300ms debounce 後只送最後一筆 `exercise`
- UI 不應卡住等待回應

### 3. 自定義亮度

自定義模式調整亮度 20 / 60 / 90。

預期：

- MQTT 送 `set_static`
- params 有 `brightness_pct`
- ACK `ok:true`

### 4. RGB 調色盤

自定義 → RGB → 點色票。

預期：

- MQTT 送 `set_static`
- params 有 `brightness_pct, r, g, b`
- ACK `ok:true`

### 5. 色溫

自定義 → 色溫 → 拖曳 slider。

預期：

- MQTT 送 `set_static`
- params 有 `brightness_pct, color_temp_k`
- ACK `ok:true`

### 6. 流水

自定義 → 燈效選流水。

預期：

- MQTT 送 `set_flow`
- ACK `ok:true`

### 7. 呼吸

自定義 → 燈效選呼吸。

預期：

- 先送 `set_static`
- 再送 `set_static_breathing`
- 兩筆 ACK 都 `ok:true`

## 六、錯誤判斷

這個虛擬裝置會主動擋下舊 payload。

若 MQTT 或 Dashboard 出現：

```text
set_custom_should_not_be_used_in_custom_brightness_only
```

代表 Dashboard 還在送舊的 `set_custom`，需要回頭修 UI / bridge。

若出現：

```text
rgb_and_kelvin_conflict
```

代表同一筆 `set_static` 同時送了 RGB 與色溫，仍需修正。

通過標準：

```text
不出現 set_custom
不出現 target_lux_range
不出現 invalid_rgb
不出現 rgb_and_kelvin_conflict
```

## 七、注意

正式硬體測試時請使用原本的：

```text
START_DASHBOARD.cmd
```

虛擬測試請使用：

```text
START_DASHBOARD_VIRTUAL_LOCAL.cmd
START_MOCK_SMARTLIGHT_LOCAL.cmd
```
