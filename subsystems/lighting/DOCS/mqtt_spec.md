# MQTT 通訊規格

## 基本設定

| 項目 | 內容 |
|---|---|
| Topic Base | `smartlight` |
| Node ID | `bedroom01` |
| 指令 Topic | `smartlight/bedroom01/cmd` |

## Topic 表

| 方向 | Topic | 用途 |
|---|---|---|
| Server / Dashboard -> Node | `smartlight/bedroom01/cmd` | 發送控制指令 |
| Node -> Server / Dashboard | `smartlight/bedroom01/ack` | 回覆指令是否接收或錯誤原因 |
| Node -> Server / Dashboard | `smartlight/bedroom01/status` | 回報目前燈光狀態 |
| Node -> Server / Dashboard | `smartlight/bedroom01/event` | 回報事件 |
| Node -> Server / Dashboard | `smartlight/bedroom01/availability` | 回報節點 online / offline |

## 情境模式範例

```json
{
  "req_id": "scene_work_001",
  "cmd": "set_scene",
  "params": {
    "scene": "work"
  }
}
```

## 自定義固定亮度範例

```json
{
  "req_id": "custom_fixed_001",
  "cmd": "set_custom",
  "params": {
    "custom_control_type": "fixed_brightness",
    "fixed_brightness_pct": 60,
    "color_mode": "cct",
    "color_temp_k": 4000,
    "effect": "static"
  }
}
```

## 狀態欄位摘要

| 欄位 | 說明 |
|---|---|
| `active_mode` | 目前模式，例：`scene` 或 `custom` |
| `active_scene` | 目前情境，例：`vacant / sleep / relax / work / exercise` |
| `custom_submode` | 自定義子模式，例：`static / breathing / flow / combo` |
| `brightness_pct` | 亮度百分比 |
| `led_output_percent` | LED 實際輸出百分比 |
| `color_temp_k` | 色溫，單位 Kelvin |
| `current_lux` | 目前環境照度 |
| `sensor_ok` | 感測器是否正常 |
| `control_state` | 控制判斷狀態 |
