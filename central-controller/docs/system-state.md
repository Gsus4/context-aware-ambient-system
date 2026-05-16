# 全局狀態管理

`SystemStateManager` 是主控程式內的全局狀態管理層，位置在 `src/smart_space/system_state.py`。它保存系統最新的即時狀態，供後續情境感知、融合控制、音樂、事件紀錄等服務讀取。

## 解耦原則

- `SystemStateManager` 不依賴 dashboard 模組，也不輸出 dashboard 專用 payload。
- Dashboard 只負責顯示、WebSocket 推送與手動控制轉送。
- 其他程式需要讀取目前空間狀態時，應使用 `app.state.system_state` 或 `GET /api/system-state`，不要讀取 `/api/dashboard`。

## Snapshot 格式

`snapshot()` 與 `GET /api/system-state` 回傳相同的頂層格式：

```json
{
  "updatedAtMs": 1710000000000,
  "operationMode": {
    "smartModeEnabled": true,
    "controlMode": "scene"
  },
  "currentContext": {
    "scene": "vacant",
    "source": "default",
    "confidence": null,
    "reason": ""
  },
  "subsystems": {
    "led": {"status": "unknown", "online": null},
    "wearable": {"status": "unknown", "online": null},
    "envComfort": {"status": "unknown", "online": null},
    "audio": {"status": "unknown", "online": null}
  },
  "sensors": {
    "heartRate": null,
    "spo2": null,
    "aqi": null,
    "temperature": null,
    "humidity": null,
    "currentLux": null
  },
  "devices": {
    "light": {"on": null, "brightness": null, "kelvin": null},
    "fan": {"on": null, "speed": null, "mode": null},
    "audio": {
      "connected": null,
      "playing": false,
      "state": "STOPPED",
      "mode": null,
      "scene": null,
      "autoModeEnabled": true,
      "volume": null,
      "track": null,
      "trackPath": null,
      "outputDevice": null,
      "progressSec": null,
      "durationSec": null
    }
  },
  "connections": {
    "light": false,
    "env": false,
    "wearable": false
  }
}
```

## Python 用法

FastAPI app 建立時會把全局狀態放在 `app.state.system_state`：

```python
state = app.state.system_state.snapshot()
scene = state["currentContext"]["scene"]
temperature = state["sensors"]["temperature"]
```

主要方法：

- `snapshot()`：取得目前狀態副本。
- `apply_mqtt_message(topic, raw)`：將 MQTT status/availability 訊息同步到全局狀態。
- `activate_system(source, reason)`：從 `vacant` 啟動為智慧模式 `relax` 情境；PIR 會使用這個入口。
- `set_mqtt_connection(name, connected)`：更新 MQTT broker 連線狀態。
- `set_operation_mode(...)`：更新智慧模式、控制模式與目前情境。
- `update_audio_state(...)`：由本機音樂播放服務同步音樂播放狀態。
- `add_context_listener(...)` / `remove_context_listener(...)`：讓音樂等服務在情境變更時做非 Dashboard 的自動協調。

## HTTP 用法

```text
GET /api/system-state
```

此 endpoint 回傳全局狀態，不經過 dashboard payload adapter。

## 啟動與融合控制來源

系統啟動後的初始情境是 `vacant`。當環境 MQTT PIR 欄位為 true 時，`SystemStateManager.activate_system(...)` 會把系統切到：

```json
{
  "operationMode": {"smartModeEnabled": true, "controlMode": "scene"},
  "currentContext": {"scene": "relax", "source": "pir"}
}
```

本機 GPIO 按鈕由 `FusionControlService` 持有。按鈕按下時，fusion engine 會以 `source="gpio_button"` 切到同樣的智慧模式 `relax` 情境，並送出 `relax` 的燈光與風扇控制；`GpioButtonService` 也會嘗試發布 controller MQTT event 供 broker 觀察。

智慧模式開啟時，AI 情境感知結果會由情境融合控制引擎用 `source="fusion_control"` 更新 `currentContext`。智慧模式關閉時，AI 融合控制不會更新情境或送出設備命令，以避免覆蓋使用者手動設定。

## 目前限制

第一版只保存即時狀態，不保存歷史紀錄，也不做重啟後恢復。事件資料庫與感測趨勢資料應在後續獨立服務中實作。
