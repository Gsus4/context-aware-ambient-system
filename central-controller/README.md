# Central Controller

## 這是什麼

這個目錄是主控系統，負責：

- 提供 Web Dashboard
- 提供 REST API / WebSocket
- 維護系統狀態
- 訂閱 MQTT 訊息
- 協調燈光、風扇、音樂與情境控制

主要程式碼在：

```text
src/smart_space/
  api/        FastAPI app、REST API、WebSocket
  mqtt/       MQTT client 與 topic
  services/   camera、music、fusion control、scene awareness
  web/        Dashboard 靜態檔
```

## 如何執行

先建立設定檔：

```bash
cp .env.example .env
```

再啟動：

```bash
./START_SMART_SPACE.sh
```

Windows：

```bat
START_SMART_SPACE.cmd
```

啟動後預設網址：

```text
http://localhost:3000
```

## 常用設定

```text
MQTT_HOST=localhost
WEARABLE_MQTT_HOST=localhost
ENV_MQTT_HOST=pi5203.local
GPIO_BUTTON_ENABLED=true
MUSIC_ENABLED=true
SCENE_AWARENESS_ENABLED=true
```

完整範例見 [.env.example](/home/pi/context-aware-ambient-system/central-controller/.env.example:1)。

## 相關文件

- [docs/system-state.md](/home/pi/context-aware-ambient-system/central-controller/docs/system-state.md:1)
- [docs/fusion-control.md](/home/pi/context-aware-ambient-system/central-controller/docs/fusion-control.md:1)
- [docs/camera-service.md](/home/pi/context-aware-ambient-system/central-controller/docs/camera-service.md:1)
- [docs/gpio-button.md](/home/pi/context-aware-ambient-system/central-controller/docs/gpio-button.md:1)
