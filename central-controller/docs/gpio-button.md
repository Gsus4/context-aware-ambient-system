# 本機 GPIO 啟動按鈕

`GpioButtonService` 位於 `src/smart_space/services/gpio_button.py`，由 `FusionControlService` 在 FastAPI lifecycle 中啟停。它負責監控本機按鈕，按下時會通知 fusion engine，並嘗試發布 MQTT event 供 broker 觀察。

按鈕是主控系統本機 GPIO，不屬於 env node。按下時會透過本機 MQTT broker client 發布 controller event：

```text
integration/smart/v1/controller/{CONTROLLER_NODE_ID}/event
```

預設 topic：

```text
integration/smart/v1/controller/smart_space_master/event
```

正常鏈路：

```text
按下按鈕
-> GpioButtonService 通知 FusionControlService
-> GpioButtonService 同時嘗試 publish controller event 到本機 MQTT broker
-> FusionControlService 將 system state 從 vacant 切到 relax
-> FusionControlService 送出 relax 的燈光與風扇控制命令
```

如果本機 MQTT publish 暫時失敗，按鈕仍會透過同程序 callback 啟動 fusion engine，不會因為 broker event 沒送出而失效。

## Raspberry Pi 4B 接線

預設使用 BCM 編號 **GPIO17**，也就是 Raspberry Pi 4B 排針上的 **physical pin 11**。

接線：

```text
button one side  -> physical pin 11 (GPIO17 / BCM17)
button other side -> physical pin 6 (GND)
```

程式使用 internal pull-up resistor，因此按鈕未按下時為 HIGH，按下時接地為 LOW。不要把按鈕接到 5V。

## BCM 與 physical pin

- `GPIO_BUTTON_PIN=17` 使用的是 BCM 編號。
- 文件中的 physical pin 11 是 Raspberry Pi 4B 排針位置。
- 如果改用其他腳位，請填 BCM 編號，不是 physical pin 編號。

## 設定

`.env` 可設定：

```text
GPIO_BUTTON_ENABLED=true
GPIO_BUTTON_PIN=17
GPIO_BUTTON_BOUNCE_SECONDS=0.1
CONTROLLER_NODE_ID=smart_space_master
```

若要停用本機按鈕：

```text
GPIO_BUTTON_ENABLED=false
```

如果不在 Raspberry Pi 上執行、`gpiozero` 無法初始化，或硬體 backend 不可用，服務會標記為 disabled，但主控 FastAPI app 仍會繼續啟動。
