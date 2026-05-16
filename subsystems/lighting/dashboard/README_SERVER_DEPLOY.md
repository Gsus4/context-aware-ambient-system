# SmartLight Dashboard Server 移機與修改說明書

## 0. 最先要改：Server 環境與 MQTT Broker 設定

正式搬到 Server 後，**所有裝置的 MQTT 都要連到 Server 端 MQTT Broker**。

也就是以下裝置或服務都應該指向同一台 Server 的 MQTT Broker，除非系統刻意分多台 Broker：

```text
Dashboard Server
LED / Node / Gateway
環境感測節點
穿戴式手環
其他子系統
```

---

## 0-1. 需要先確認的 Server 資訊

| 項目 | 目前狀態 |
|---|---|
| Server IP | `[待補資料]` |
| MQTT Broker Port | `1883` |
| Dashboard Web Port | `3000` |
| SmartLight Node ID | `bedroom01` |
| 是否所有裝置都接同一台 MQTT Broker | `[待確認]` |

假設 Server IP 是：

```text
127.0.0.1
```

則 Dashboard、Node / Gateway、感測節點、穿戴式裝置都要改成連：

```text
mqtt://127.0.0.1:1883
```

---

## 0-2. 正式版 START_DASHBOARD.cmd 建議內容

正式交給 Server 端時，建議 `START_DASHBOARD.cmd` 改成以下內容：

```cmd
@echo off
cd /d "%~dp0"

echo ============================================================
echo SmartLight Dashboard Server
echo Dashboard : http://localhost:3000
echo ============================================================

REM ============================================================
REM MQTT Broker Settings
REM 請依 Server 實際 IP 修改
REM ============================================================

set LIGHT_MQTT_URL=mqtt://127.0.0.1:1883
set ENV_MQTT_URL=mqtt://127.0.0.1:1883
set WEARABLE_MQTT_URL=mqtt://127.0.0.1:1883

REM SmartLight Node ID
set SMARTLIGHT_NODE_ID=bedroom01

REM ============================================================
REM 正式版預設關閉測試型自動 LED 控制
REM 智慧模式 OFF 時，不應由 auto_activity_still 背景控制 LED
REM ============================================================

set AUTO_ACTIVITY_LED_ENABLED=false
set AUTO_HR_LED_ENABLED=false

REM ============================================================
REM Install dependencies if needed
REM ============================================================

if not exist node_modules (
  echo [Dashboard] Installing dependencies...
  npm.cmd install
)

echo [Dashboard] Starting...
npm.cmd start

pause
```

如果 Server IP 不是 `127.0.0.1`，請改這三行：

```cmd
set LIGHT_MQTT_URL=mqtt://<SERVER_IP>:1883
set ENV_MQTT_URL=mqtt://<SERVER_IP>:1883
set WEARABLE_MQTT_URL=mqtt://<SERVER_IP>:1883
```

---

## 0-3. Server 防火牆與連接埠

Server 端需開啟：

| Port | 用途 |
|---:|---|
| `1883` | MQTT Broker |
| `3000` | Dashboard Web |
| `[待補資料]` | 其他子系統若有額外服務需補充 |

如果其他電腦或手機要連 Dashboard，使用：

```text
http://<SERVER_IP>:3000
```

例如：

```text
http://127.0.0.1:3000
```

---

# 1. 目前 Dashboard 最新版修改摘要

## 1-1. 自定義模式已改成固定亮度控制

目前最新版 Dashboard 已移除自定義模式中的：

```text
目標照度補光
target_lux_range
target_lux
tolerance_lux
target_lux_min
target_lux_max
min_output
max_output
cmd:set_custom
```

保留：

```text
亮度 slider
色溫 / RGB 二選一
RGB 調色盤
自選顏色
RGB 紅 / 綠 / 藍 slider
靜態
呼吸
流水
```

修改原因：舊版曾送出 `custom_control_type:"target_lux_range"` 搭配 `color_mode:"rgb"` 與 `r/g/b`，底層回覆 `invalid_command_payload / invalid_rgb`，因此自定義模式改為只保留固定亮度控制。

---

## 1-2. 自定義模式現在的 MQTT 指令

### A. 亮度 + 色溫

```json
{
  "cmd": "set_static",
  "params": {
    "brightness_pct": 60,
    "color_temp_k": 3900
  }
}
```

### B. 亮度 + RGB

```json
{
  "cmd": "set_static",
  "params": {
    "brightness_pct": 60,
    "r": 255,
    "g": 160,
    "b": 80
  }
}
```

### C. 呼吸燈

先送：

```json
{
  "cmd": "set_static",
  "params": {
    "brightness_pct": 60,
    "r": 255,
    "g": 160,
    "b": 80
  }
}
```

再送：

```json
{
  "cmd": "set_static_breathing",
  "params": {
    "enabled": true,
    "speed": "slow",
    "strength": "medium"
  }
}
```

### D. 流水燈

```json
{
  "cmd": "set_flow",
  "params": {
    "flow_preset": "soft_rainbow",
    "flow_speed": "medium",
    "flow_brightness": 64,
    "soft_mode": false
  }
}
```

---

## 1-3. 不應再出現的 MQTT 內容

正式測試時，MQTT 不應再看到：

```text
cmd:set_custom
custom_control_type:target_lux_range
target_lux_range
invalid_rgb
rgb_and_kelvin_conflict
```

如果 Server 端測試時仍看到這些內容，代表可能有以下狀況：

| 可能原因 | 處理方式 |
|---|---|
| 使用到舊版 Dashboard | 確認是否啟動正確資料夾 |
| 舊 `led_profiles.user.json` 沒被更新 | 檢查 user profile 是否還有 `target_lux_range` |
| 瀏覽器快取舊 JS | 按 `Ctrl + F5` |
| 另一個舊 Dashboard Server 還在跑 | `taskkill /F /IM node.exe` 後只重開新版 |

---

# 2. 正式版 Dashboard 建議保留檔案

正式部署到 Server 時，建議只保留以下結構：

```text
dashboard/
├─ index.html
├─ package.json
├─ package-lock.json
├─ START_DASHBOARD.cmd
├─ README.md
├─ css/
│  └─ dashboard.css
├─ js/
│  ├─ config.js
│  ├─ dashboard-app.js
│  ├─ data-adapter.js
│  └─ led-advanced-panel.js
└─ server/
   ├─ .env.example
   ├─ dashboard-mqtt-bridge.js
   ├─ config/
   │  └─ led_profiles.default.json
   └─ data/
      └─ led_profiles.user.json
```

一定不要刪：

```text
server/dashboard-mqtt-bridge.js
server/config/led_profiles.default.json
server/data/led_profiles.user.json
```

`led_profiles.user.json` 是使用者偏好設定。刪除後會回到預設值。

---

# 3. 可以清理或移除的檔案

以下檔案不影響正式 Server 運作，可以刪除或移到備份資料夾：

```text
mock_smartlight_device.js
START_MOCK_SMARTLIGHT_LOCAL.cmd
START_DASHBOARD_VIRTUAL_LOCAL.cmd
START_VIRTUAL_VALIDATION_ALL.cmd
README_VIRTUAL_VALIDATION.md
node_modules/
_removed_old_files/
mock/
rwd-check/
dashboard-dev-server.js
example-node-server.js
start-dashboard.bat
README_*.md
README_*.txt
*_README*.md
*_README*.txt
TODO*.txt
TODO*.md
*.bak
*.old
*.tmp
*.backup
```

說明：

| 類型 | 是否正式需要 |
|---|---|
| Mock 虛擬裝置 | 不需要，僅 PC 虛擬驗證用 |
| Virtual 啟動檔 | 不需要，僅本機測試用 |
| `node_modules` | 不建議打包，Server 端 `npm install` 重建 |
| 舊 README / TODO | 不需要，避免混淆 |
| `rwd-check` | 不需要，僅截圖檢查用 |

---

# 4. 清理用 PowerShell 指令

建議先移到 `_removed_old_files`，確認可執行後再刪除。

```powershell
cd C:\Users\User\Desktop\SmartLight_V5\dashboard

New-Item -ItemType Directory -Force -Path "_removed_old_files" | Out-Null

# node_modules 可由 npm install 重建
if (Test-Path "node_modules") {
  Move-Item "node_modules" "_removed_old_files\node_modules" -Force
}

# 虛擬驗證工具
$virtualFiles = @(
  "mock_smartlight_device.js",
  "START_MOCK_SMARTLIGHT_LOCAL.cmd",
  "START_DASHBOARD_VIRTUAL_LOCAL.cmd",
  "START_VIRTUAL_VALIDATION_ALL.cmd",
  "README_VIRTUAL_VALIDATION.md"
)

foreach ($file in $virtualFiles) {
  if (Test-Path $file) {
    Move-Item $file "_removed_old_files\$file" -Force
  }
}

# 測試資料夾
if (Test-Path "mock") {
  Move-Item "mock" "_removed_old_files\mock" -Force
}

if (Test-Path "rwd-check") {
  Move-Item "rwd-check" "_removed_old_files\rwd-check" -Force
}

# 多餘啟動檔，只保留 START_DASHBOARD.cmd
Get-ChildItem -Path ".\*" -File |
Where-Object {
  ($_.Extension -in ".cmd", ".bat") -and
  $_.Name -ne "START_DASHBOARD.cmd"
} |
ForEach-Object {
  Move-Item $_.FullName "_removed_old_files\$($_.Name)" -Force
}

# 多餘 README / TODO，只保留 README.md
Get-ChildItem -Path ".\*" -File |
Where-Object {
  ($_.Name -like "README*" -or $_.Name -like "*README*" -or $_.Name -like "TODO*") -and
  $_.Name -ne "README.md"
} |
ForEach-Object {
  Move-Item $_.FullName "_removed_old_files\$($_.Name)" -Force
}

# 暫存檔
Get-ChildItem -Path ".\*" -File |
Where-Object {
  $_.Extension -in ".bak", ".old", ".tmp", ".backup"
} |
ForEach-Object {
  Move-Item $_.FullName "_removed_old_files\$($_.Name)" -Force
}
```

確認正式 Dashboard 正常啟動後，再刪除備份：

```powershell
Remove-Item "_removed_old_files" -Recurse -Force
```

---

# 5. Server 端安裝與啟動

## 5-1. 安裝套件

第一次部署時，在 `dashboard` 資料夾執行：

```cmd
npm.cmd install
```

若使用 PowerShell 遇到 `npm.ps1` 權限錯誤，請改用 CMD 或使用：

```cmd
npm.cmd install
```

---

## 5-2. 啟動 Dashboard

```cmd
cd C:\Users\User\Desktop\SmartLight_V5\dashboard
npm.cmd start
```

或雙擊：

```text
START_DASHBOARD.cmd
```

啟動後本機可開：

```text
http://localhost:3000
```

其他裝置可開：

```text
http://<SERVER_IP>:3000
```

---

# 6. MQTT Topic 說明

LED 子系統使用：

```text
smartlight/bedroom01/cmd
smartlight/bedroom01/ack
smartlight/bedroom01/status
smartlight/bedroom01/event
smartlight/bedroom01/availability
```

目前 Dashboard 沒有改 MQTT topic。

若 Node ID 不是 `bedroom01`，需同步確認：

```text
Dashboard 設定
Node / Gateway 設定
MCU / Pico W 設定
MQTT topic
```

---

# 7. Server 端測試流程

## 7-1. 開 MQTT 監聽

```cmd
mosquitto_sub -h <SERVER_IP> -t smartlight/bedroom01/# -v
```

例如：

```cmd
mosquitto_sub -h 127.0.0.1 -t smartlight/bedroom01/# -v
```

---

## 7-2. 基本連線測試

| 測試 | 預期 |
|---|---|
| 開啟 Dashboard | SmartLight 顯示 online |
| `get_status` | ACK OK，回 status |
| availability | 顯示 online |

---

## 7-3. 情境模式測試

| 操作 | 預期 MQTT |
|---|---|
| 無人 | `set_scene vacant`，ACK OK |
| 睡眠 | `set_scene sleep`，ACK OK |
| 放鬆 | `set_scene relax`，ACK OK |
| 工作 | `set_scene work`，ACK OK |
| 運動 | `set_scene exercise`，ACK OK |
| 快速 sleep → exercise | 以前端最後選擇為準 |
| power off | 立即送出，不被 debounce 延遲 |

---

## 7-4. 自定義模式測試

| 操作 | 預期 MQTT |
|---|---|
| 亮度 20 / 60 / 90 | `set_static` |
| 色溫 slider | `set_static + color_temp_k` |
| RGB 調色盤 | `set_static + r/g/b` |
| 呼吸 | `set_static` → `set_static_breathing` |
| 流水 | `set_flow` |

正式測試通過標準：

```text
不出現 set_custom
不出現 target_lux_range
不出現 invalid_rgb
不出現 rgb_and_kelvin_conflict
```

---

# 8. 智慧模式與 auto_activity_still

`auto_activity_still` 是智慧模式底下的自動 LED 控制，不是單純查狀態。它會送 `set_static` 改 LED。

正式規則：

| 狀態 | auto_activity_still |
|---|---|
| 智慧模式 ON | 可以執行 |
| 智慧模式 OFF | 不應執行 |
| 自定義手動操作中 | 不應干擾 |
| power off 後 | 不應重新點亮 |

正式版建議預設：

```cmd
set AUTO_ACTIVITY_LED_ENABLED=false
set AUTO_HR_LED_ENABLED=false
```

如果要測智慧模式自動 LED，再手動開啟。

---

# 9. 已知狀態與驗證結果

目前 PC 虛擬驗證已通過：

```text
set_scene：通過
set_static 色溫：通過
set_static RGB：通過
set_static_breathing：通過
set_flow：通過
未出現 set_custom
未出現 target_lux_range
未出現 invalid_rgb
未出現 rgb_and_kelvin_conflict
```

Dashboard 在虛擬模式下使用本機 MQTT broker `mqtt://127.0.0.1:1883`，並確認 Auto State → LED 與 Auto HR LED Test 為 disabled。

---

# 10. 移交 Server 前最後檢查

在 Dashboard 資料夾執行：

```cmd
node --check js\config.js
node --check js\dashboard-app.js
node --check js\data-adapter.js
node --check js\led-advanced-panel.js
node --check server\dashboard-mqtt-bridge.js
```

確認沒有語法錯誤後再打包。

打包時不要包含：

```text
node_modules/
_removed_old_files/
mock/
rwd-check/
```

---

# 11. 給 Server 端的最終交接摘要

```text
本版 Dashboard 已完成自定義模式簡化：移除目標照度補光，只保留固定亮度、色溫 / RGB、調色盤、自選顏色、呼吸、流水。

自定義模式不再送 set_custom 或 target_lux_range，改用 set_static、set_flow、set_static_breathing。PC 虛擬 SmartLight 驗證已通過，主要 MQTT 指令皆可收到 ACK OK，且未再出現 invalid_rgb。

Server 端部署時，最重要的是先修改 START_DASHBOARD.cmd 中的 MQTT Broker IP，讓 LIGHT_MQTT_URL、ENV_MQTT_URL、WEARABLE_MQTT_URL 都指向 Server 端 MQTT Broker。正式測試時請確認 MQTT 不再出現 set_custom、target_lux_range、invalid_rgb。
```
