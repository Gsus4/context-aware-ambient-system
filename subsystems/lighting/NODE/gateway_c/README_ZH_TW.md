# SmartLight Gateway C（V5.0.9）

本版 gateway 對外模式結構以 `scene / custom` 為準；舊 `set_auto` 只做相容映射。

## 對外 status 表示
- `active_mode = scene / custom`
- 當 `active_mode = custom` 時，使用 `custom_submode = static / breathing / flow / combo`

## 內部執行層
為保留既有主鏈相容性，MCU / UART status 仍沿用內部執行模式：
- scene
- static/manual
- flow
- auto

Gateway 會在 JSON status 建構時，將內部執行層映射成對外正式模式。

## 核心職責
- MQTT ↔ UART / driver 橋接
- 指令轉發
- ack / status / event / availability 發布
- `get_status` 的 req_id 關聯維持
- 內部模式 → 對外模式映射（scene / custom / auto）
