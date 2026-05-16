# Context-Aware Ambient System

這是一個以 Raspberry Pi、Pico W、MQTT 與 Web Dashboard 組成的智慧空間控制系統。

目前 repo 主要包含：

- `central-controller/`：主控 server、Dashboard、API、情境控制
- `subsystems/lighting/`：照明控制子系統
- `subsystems/environment/`：環境感測與風扇控制子系統
- `subsystems/wearable/`：穿戴式感測子系統

## 系統架構

![系統架構圖](docs/images/system-architecture.png)

## 如何執行

目前主要入口在 `central-controller/`：

```bash
cd central-controller
cp .env.example .env
./START_SMART_SPACE.sh
```

啟動後可在瀏覽器開啟：

```text
http://localhost:3000
```

## 補充

- 這個專案需要對應的 Raspberry Pi、Pico W、MQTT broker 與感測硬體才能完整運作
- 主控系統的設定與 API 說明在 [central-controller/README.md](/home/pi/context-aware-ambient-system/central-controller/README.md:1)
