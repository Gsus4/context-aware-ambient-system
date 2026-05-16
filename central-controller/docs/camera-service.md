# Camera Service

Camera service 在 FastAPI lifecycle 啟動時建立，掛在 `app.state.camera_service`。它的責任很窄：啟動 repo 內的 MediaMTX、讓 Raspberry Pi camera 發布到固定 path，並把可用的串流 URL 發布到 API、Dashboard payload 與 `/api/system-state`。

## 串流路徑

預設 path 是 `space`。

```text
RTSP:    rtsp://<host>:8554/space
WebRTC:  http://<host>:8889/space
WHEP:    http://<host>:8889/space/whep
HLS:     /api/camera/hls/space/index.m3u8
Snapshot /api/camera/snapshot.jpg
```

使用原則：

- Dashboard 優先使用 MediaMTX WebRTC，降低瀏覽器端延遲。
- 空間情境識別、OpenCV、ffplay、GStreamer 等內部程式優先使用 RTSP。
- HLS 保留為 Dashboard fallback；它有 playlist/segment buffer，不適合作為最低延遲預覽。
- Snapshot 只作為 debug/fallback，不代表即時影片串流。Service 會優先用 `ffmpeg` 從本機 RTSP stream 抽一張 JPEG，避免 WebRTC 串流使用中時 `rpicam-still` 搶相機失敗；若 `ffmpeg` 不可用或抽幀失敗，才 fallback 到 `rpicam-still` / `libcamera-still`。

同一個區域網路內可以用 RPi hostname 測試：

```bash
ffplay rtsp://s10RPi4:8554/space
```

## API contract

`GET /api/camera/status` 回傳：

```json
{
  "connected": true,
  "streamPath": "space",
  "rtspUrl": "rtsp://s10RPi4:8554/space",
  "webrtcUrl": "http://s10RPi4:8889/space",
  "whepUrl": "http://s10RPi4:8889/space/whep",
  "hlsUrl": "/api/camera/hls/space/index.m3u8",
  "snapshotUrl": "/api/camera/snapshot.jpg",
  "lastFrameAtMs": 1710000000000,
  "lastError": null
}
```

`GET /api/system-state` 也會在 `devices.camera` 暴露同樣的 camera metadata，並在 `sensors` 中提供 Dashboard 需要的 `cameraWebrtcUrl`、`cameraWhepUrl`、`cameraHlsUrl`、`cameraSnapshotUrl`。

## 設定

常用 `.env`：

```text
CAMERA_ENABLED=true
CAMERA_STREAM_PATH=space
CAMERA_PUBLIC_HOST=s10RPi4
CAMERA_RTSP_PORT=8554
CAMERA_HLS_PORT=8888
CAMERA_WEBRTC_PORT=8889
CAMERA_WIDTH=960
CAMERA_HEIGHT=720
CAMERA_FPS=24
CAMERA_BITRATE=2000000
CAMERA_MEDIAMTX_BIN=mediamtx/mediamtx
CAMERA_MEDIAMTX_CONFIG=mediamtx/mediamtx.yml
```

`CAMERA_PUBLIC_HOST` 建議在實機設為 `s10RPi4`。未設定時，backend 會用 `socket.gethostname()` 產生對外 URL。

## Lifecycle

啟動流程：

1. FastAPI 建立 `SpaceCameraService`。
2. Service 讀取 `mediamtx/mediamtx.yml`，產生 runtime config。
3. Runtime config 只覆寫 `paths`，讓 `space` 使用 MediaMTX native `rpiCamera` source。
4. MediaMTX 提供 RTSP、WebRTC、HLS 輸出。
5. Service 發布狀態到 Dashboard 與 system state。

停止 FastAPI 時會停止 MediaMTX process 並清掉 runtime config。

修改 `mediamtx/mediamtx.yml` 或 camera `.env` 後，需要重啟 backend，正在執行的 MediaMTX 不會自動套用新設定。
