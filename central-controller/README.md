# 智慧情境感知與空間氛圍控制系統

本專案是部署在主控 Raspberry Pi 上的智慧空間主控系統。第一階段重構重點是保留既有 Dashboard 前端版面與控制體驗，將後端改為 Python/FastAPI，並把 MQTT、狀態管理、控制轉送與後續服務擴充點拆成可維護的架構。

## 目前功能

- 提供 Web Dashboard：`http://localhost:3000`
- 透過 WebSocket 將即時狀態推送到 Dashboard
- 透過 REST/WebSocket 接收 Dashboard 手動控制指令
- 透過 MQTT 訂閱子系統狀態並轉送控制命令
- 提供與 Dashboard 解耦的全局狀態管理與 `/api/system-state`
- 內建空間影像串流服務，啟動 repo-local MediaMTX，提供 RTSP/WebRTC/HLS/snapshot metadata
- 內建 AI 情境感知服務，持續維護 6 張影像 buffer，智慧模式開啟時以影像序列與心率產生 AI observation
- 內建情境融合控制引擎，依 AI 情境更新 `/api/system-state` 並輸出燈光、風扇與音樂控制；AI 連續 2 次判斷無人才切入 `vacant`
- 內建本機 GPIO 啟動按鈕 reader，Raspberry Pi 4B 預設使用 GPIO17 / physical pin 11
- 支援多 broker 架構：
  - 燈光與穿戴式裝置預設走主控端 broker
  - 環境監測與風扇預設走 Env_Node broker
- 保留 LED 情境設定與自定義 profile 讀寫
- 內建本機音樂播放服務，支援情境自動播放、手動控制、音量與輸出裝置切換
- 預留情境感知、融合控制、事件資料庫服務介面

## 快速啟動

需要先安裝 `uv`。

```bash
cd /path/to/context-aware-ambient-system/central-controller
./START_SMART_SPACE.sh
```

這個入口會啟動主控 FastAPI app，包含 Dashboard 靜態服務、WebSocket/API、MQTT bridge、全局狀態管理、本機音樂播放服務、情境融合控制引擎與本機 GPIO 按鈕 reader。

Windows 可執行：

```bat
START_SMART_SPACE.cmd
```

舊的 `START_DASHBOARD.sh` / `START_DASHBOARD.cmd` 仍保留給只需要沿用 Dashboard 名稱的啟動流程，但新開發應使用 `START_SMART_SPACE.*` 作為整個專案入口。

瀏覽器開啟：

```text
http://localhost:3000
```

## 設定

複製 `.env.example` 為 `.env` 後依實機網路調整。

常用設定：

```text
PORT=3000
SMART_SPACE_HOST=0.0.0.0
SMART_SPACE_RELOAD=false
DASHBOARD_DIR=src/smart_space/web/dashboard

MQTT_HOST=localhost
MQTT_PORT=1883
SMARTLIGHT_NODE_ID=bedroom01
CONTROLLER_NODE_ID=smart_space_master

WEARABLE_MQTT_HOST=localhost
WEARABLE_NODE_ID=pico_wearable01

ENV_MQTT_HOST=pi5203.local
ENV_SENSOR_NODE_ID=rpi_env01
FAN_NODE_ID=rpi_env02

GPIO_BUTTON_ENABLED=true
GPIO_BUTTON_PIN=17
GPIO_BUTTON_BOUNCE_SECONDS=0.1

MUSIC_ENABLED=true
MUSIC_DIR=music
MUSIC_DEFAULT_VOLUME=50

GOOGLE_API_KEY=
SCENE_AWARENESS_ENABLED=true
SCENE_AWARENESS_MODEL=gemma-4-31b-it
SCENE_AWARENESS_LOG_PATH=.runtime/scene_awareness.log
```

音樂目錄預設為 `music/`。可播放情境目錄為 `music/work`、`music/relax`、`music/sleep`、`music/exercise`；`music/working` 會作為 `work` 的 fallback。無人情境 `vacant` 會停止播放，不建立 `music/vacant`。

GPIO 啟動按鈕預設接線為：按鈕一端接 Raspberry Pi 4B **physical pin 11 (GPIO17/BCM17)**，另一端接 **physical pin 6 (GND)**。程式使用 internal pull-up，按下時讀到 LOW；不要把按鈕接到 5V。更多細節見 `docs/gpio-button.md`。

智慧模式開啟且目前情境不是 `vacant` 時，情境感知會送出 AI 推論 request 並由融合控制引擎套用結果。`ACTIVE` 與 `UNKNOWN` 不改變目前情境；`EMPTY` 必須連續出現 2 次才進入 `vacant` 節能狀態。系統處於 `vacant` 後，情境感知只維持影像 buffer，不再送 AI request，直到 PIR 或 GPIO 按鈕重新啟動系統。智慧模式關閉時不輸出控制命令，避免覆蓋使用者手動設定。控制規則見 `docs/fusion-control.md`。

## 架構

```text
src/smart_space/
  api/          FastAPI app、REST API、WebSocket、靜態 Dashboard
  core/         設定與共用基礎元件
  dashboard/    Dashboard 狀態、命令轉換、LED profile
  mqtt/         MQTT topic 規劃與 client manager
  services/     Camera / LLM / 控制引擎 / 音樂 / DB 服務介面
  system_state.py  全局即時狀態管理，供非 Dashboard 程式讀取
  web/          Dashboard 前端靜態檔
```

Dashboard 前端仍放在：

```text
src/smart_space/web/dashboard/index.html
src/smart_space/web/dashboard/js/
src/smart_space/web/dashboard/css/
```

舊版目錄已保留為 `dashboard_old/`，僅供對照，不再作為執行入口或靜態檔來源。

## MQTT Topic

燈光：

```text
smartlight/{SMARTLIGHT_NODE_ID}/status
smartlight/{SMARTLIGHT_NODE_ID}/ack
smartlight/{SMARTLIGHT_NODE_ID}/availability
smartlight/{SMARTLIGHT_NODE_ID}/event
smartlight/{SMARTLIGHT_NODE_ID}/cmd
```

穿戴式裝置：

```text
integration/smart/v1/wearable/{WEARABLE_NODE_ID}/status
integration/smart/v1/wearable/{WEARABLE_NODE_ID}/availability
```

環境監測與風扇：

```text
integration/smart/v1/env/{ENV_SENSOR_NODE_ID}/status
integration/smart/v1/env/{ENV_SENSOR_NODE_ID}/event
integration/smart/v1/env/{ENV_SENSOR_NODE_ID}/availability
integration/smart/v1/env/{FAN_NODE_ID}/status
integration/smart/v1/env/{FAN_NODE_ID}/ack
integration/smart/v1/env/{FAN_NODE_ID}/availability
integration/smart/v1/env/{FAN_NODE_ID}/command
```

## HTTP / WebSocket API

```text
GET  /api/dashboard
GET  /api/system-state
GET  /api/led-profiles
POST /api/led-profiles
POST /api/led-profiles/reset
POST /api/smartlight/command
GET  /api/music/status
GET  /api/music/outputs
POST /api/music/command
GET  /api/camera/status
GET  /api/camera/snapshot.jpg
GET  /api/fusion-control/status
GET  /api/scene-awareness/status
WS   /ws/dashboard
```

`/api/system-state` 是其他程式取得目前空間狀態的正式入口，回傳子系統狀態、運作模式、當前情境、感測器與裝置狀態。Dashboard 專用的 `/api/dashboard` 只保留給前端顯示契約使用。

Dashboard command 目前支援：

```text
get_status
set_mode
set_auto
set_smart_mode
set_led_power
set_brightness
set_kelvin
set_color
set_advanced_light
set_custom_light
apply_current_light
set_fan_level
set_fan_mode
```

Music command 目前支援：

```text
get_status
play
pause
resume
toggle
stop
next
prev
set_auto_mode
play_scene
set_volume
set_output
```

情境感知 CLI 監控：

```bash
uv run python -m smart_space.scene_awareness_monitor --log-path .runtime/scene_awareness.log
```

每次 Smart Space runtime 啟動時會清空 `SCENE_AWARENESS_LOG_PATH`。log 會記錄 service 啟停、智慧模式 enable/disable、camera frame 狀態、request start、response received、discarded response 與 request failure。

GPIO button reader 由 `FusionControlService` 在 FastAPI lifecycle 中啟停。若不是 Raspberry Pi、`gpiozero` 初始化失敗，或 `GPIO_BUTTON_ENABLED=false`，服務會停用但不阻止主控程式啟動。按鈕按下時會直接通知 fusion engine 切回 `relax`，並同時嘗試透過本機 broker client 發布事件到 `integration/smart/v1/controller/{CONTROLLER_NODE_ID}/event`，預設為 `integration/smart/v1/controller/smart_space_master/event`。

## 測試

```bash
uv run pytest -q
uv run python -m compileall -q src
```

## 後續擴充方向

後續主控系統建議依序加入：

1. SQLite 事件資料庫：記錄感測資料、控制指令、情境判斷與錯誤事件。
2. 更完整的歷史趨勢與控制效果評估。

目前 `src/smart_space/services/interfaces.py` 已定義這些服務的接入介面，後續實作時應保持 Dashboard 只負責監控與手動控制轉送，避免再次把所有功能塞回單一檔案。

全局狀態管理的資料格式與使用方式見 `docs/system-state.md`。Camera service 的串流 URL、設定與使用方式見 `docs/camera-service.md`。融合控制與 GPIO 按鈕見 `docs/fusion-control.md`、`docs/gpio-button.md`。
