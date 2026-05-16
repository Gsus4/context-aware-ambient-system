# Environment Subsystem

這個子系統負責空間中的「環境狀態」與「風扇執行」兩件事。

- `sensing/`：蒐集溫度、濕度、空氣品質與 PIR 相關資訊
- `fan-control/`：接收命令、執行風扇控制、回報狀態

它在整體系統中的角色，是提供主控端做情境判斷與自動控制所需的空間條件。

## Structure

```text
sensing/
  picow/        Pico W side sensor collection
  rpi/app/      Raspberry Pi side application

fan-control/
  app/          Fan control runtime
  driver_versions/ Linux driver-related code
  Pico_W/       MCU-side assets
```
