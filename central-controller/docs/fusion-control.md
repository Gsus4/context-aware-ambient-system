# 情境融合控制引擎

`FusionControlService` 位於 `src/smart_space/services/fusion_control.py`。它只在智慧模式開啟時生效，接收 AI 情境感知結果，更新 `/api/system-state`，並輸出燈光與風扇控制；音樂則透過 `SystemStateManager` 的 context listener 自動跟隨情境。

## AI 情境對應

| AI `scene_state` | 系統情境 |
| --- | --- |
| `EMPTY` | `vacant` |
| `SLEEPING` | `sleep` |
| `WORKING` | `work` |
| `RELAXING` | `relax` |
| `EXERCISING` | `exercise` |

`ACTIVE`、`UNKNOWN` 或其他未定義結果不會改變目前情境，也不會送出控制命令。

`EMPTY` 必須連續出現 2 次才會切換到 `vacant`。中間只要出現非 `EMPTY` 的有效有人情境，無人計數就會重置。

當系統目前情境已是 `vacant` 時，情境感知服務會繼續收集影像 buffer，但不會送出新的 AI 推論 request。後續需由 PIR 或 GPIO 按鈕重新啟動。

GPIO 按鈕由 `FusionControlService` 啟動並持有。按鈕按下時，`GpioButtonService` 會用同程序 callback 通知融合控制引擎立即套用 `relax`，並同時嘗試發布 controller MQTT event；融合控制引擎也會訂閱該 topic，因此外部 event 仍可走 MQTT 路徑。

融合控制引擎內部維護一個簡單狀態循環：

```text
vacant -> GPIO/PIR activation -> occupied(relax)
occupied -> AI scene changes -> occupied(scene)
occupied -> 2 consecutive EMPTY -> vacant
```

進入 `vacant` 後，後續 EMPTY 不會重複送出 vacant 控制。GPIO/PIR 啟動後，啟動前已送出的舊 AI request 若回傳 EMPTY，會被視為 stale observation 並丟棄，避免剛切回 `relax` 又被舊的無人判斷覆蓋。

## GPIO 按鈕事件

融合控制引擎會訂閱：

```text
integration/smart/v1/controller/{CONTROLLER_NODE_ID}/event
```

按鈕事件 payload：

```json
{
  "event": {
    "eventType": "gpio_button_pressed",
    "source": "gpio_button",
    "pin": 17,
    "targetScene": "relax",
    "pressCount": 1,
    "tsMs": 1710000000000
  }
}
```

收到 `gpio_button_pressed` 後，融合控制引擎會把系統切到智慧模式 `relax`，並用一般情境切換流程送出燈光與風扇控制。若 MQTT event 沒有送出，按鈕 callback 仍會啟動同一段 fusion 邏輯。

## 控制輸出

燈光使用 SmartLight MQTT command topic：

```json
{
  "req_id": "fusion_light_1710000000000",
  "cmd": "set_scene",
  "params": {"scene": "relax"}
}
```

風扇使用 env fan command topic：

```json
{
  "command": "set_mode",
  "params": {"mode": "MANUAL"},
  "request_id": "fusion_fan_1710000000000",
  "source": "fusion_control"
}
```

```json
{
  "command": "set_fan_level",
  "params": {"fan_level": 5},
  "request_id": "fusion_fan_1710000000000",
  "source": "fusion_control"
}
```

`vacant` 會使用 `mode="OFF"` 並設定 `fan_level=0`。

## 風扇規則

基礎風速：

| 情境 | 風速 |
| --- | --- |
| `vacant` | 0 |
| `sleep` | 3 |
| `work` | 5 |
| `relax` | 5 |
| `exercise` | 8 |

感測器加成：

- temperature >= 28：加 1；temperature >= 31：再加 1。
- humidity >= 70：加 1。
- AQI >= 150：加 1；AQI >= 200：再加 1。
- heartRate >= 100：加 1；heartRate >= 120：再加 1。

缺資料不加成，最後風速限制在 0 到 10。

## 智慧模式保護

如果 `/api/system-state` 的 `operationMode.smartModeEnabled` 是 false，融合控制會直接略過 AI observation，不更新 `currentContext`，也不送燈光或風扇命令。
