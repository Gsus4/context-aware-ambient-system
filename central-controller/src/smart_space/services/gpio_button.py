from __future__ import annotations

import json
import time
from collections.abc import Callable
from typing import Any

from smart_space.core.settings import Settings
from smart_space.mqtt.topics import Topics


ButtonFactoryLoader = Callable[[], Callable[..., Any] | None]
MqttClientFactory = Callable[[], Any | None]
PressHandler = Callable[[dict[str, Any]], None]


class GpioButtonService:
    def __init__(
        self,
        settings: Settings,
        *,
        button_factory: ButtonFactoryLoader | None = None,
        mqtt_client_factory: MqttClientFactory | None = None,
        on_pressed: PressHandler | None = None,
    ) -> None:
        self.settings = settings
        self.topics = Topics(settings)
        self.button_factory = button_factory or self._load_gpiozero_button
        self.mqtt_client_factory = mqtt_client_factory or self._load_mqtt_client
        self.on_pressed = on_pressed
        self._button: Any | None = None
        self._mqtt_client: Any | None = None
        self._mqtt_connected = False
        self._status: dict[str, Any] = {
            "enabled": bool(settings.gpio_button_enabled),
            "running": False,
            "mqttConnected": False,
            "pin": settings.gpio_button_pin,
            "pullUp": True,
            "bounceSeconds": settings.gpio_button_bounce_seconds,
            "reason": "",
            "pressCount": 0,
            "lastError": None,
        }

    def start(self) -> dict[str, Any]:
        if not self.settings.gpio_button_enabled:
            self._status.update({"enabled": False, "running": False, "reason": "disabled_by_settings"})
            return self.status()
        if self._button:
            return self.status()
        factory = self.button_factory()
        if factory is None:
            self._status.update({"enabled": False, "running": False, "reason": "gpio_button_unavailable"})
            return self.status()
        try:
            self._button = factory(
                self.settings.gpio_button_pin,
                pull_up=True,
                bounce_time=self.settings.gpio_button_bounce_seconds,
            )
            self._button.when_pressed = self._handle_pressed
            self._start_mqtt()
            self._status.update({"enabled": True, "running": True, "reason": "ready"})
        except Exception as error:
            self._button = None
            self._status.update({
                "enabled": False,
                "running": False,
                "reason": "gpio_button_initialization_failed",
                "lastError": str(error),
            })
        return self.status()

    def stop(self) -> dict[str, Any]:
        if self._button is not None:
            try:
                self._button.close()
            finally:
                self._button = None
        self._stop_mqtt()
        self._status["running"] = False
        return self.status()

    def status(self) -> dict[str, Any]:
        return dict(self._status)

    def _handle_pressed(self) -> None:
        self._status["pressCount"] += 1
        event = self._button_event()
        self._publish_pressed_event(event)
        if self.on_pressed:
            self.on_pressed(event)

    def _start_mqtt(self) -> None:
        client = self.mqtt_client_factory()
        if client is None:
            self._status.update({"mqttConnected": False, "lastError": "mqtt_client_unavailable"})
            return
        self._mqtt_client = client
        if self.settings.mqtt_username and hasattr(client, "username_pw_set"):
            client.username_pw_set(self.settings.mqtt_username, self.settings.mqtt_password or None)
        if hasattr(client, "on_connect"):
            client.on_connect = self._on_mqtt_connect
        if hasattr(client, "on_disconnect"):
            client.on_disconnect = self._on_mqtt_disconnect
        try:
            result = client.connect(self.settings.mqtt_host, self.settings.mqtt_port, keepalive=30)
            self._mqtt_connected = result in (0, None)
            self._status["mqttConnected"] = self._mqtt_connected
            self._status["lastError"] = None if self._mqtt_connected else f"mqtt_connect_failed:{result}"
            if hasattr(client, "loop_start"):
                client.loop_start()
        except Exception as error:
            self._mqtt_connected = False
            self._status.update({"mqttConnected": False, "lastError": str(error)})

    def _stop_mqtt(self) -> None:
        client = self._mqtt_client
        self._mqtt_client = None
        self._mqtt_connected = False
        self._status["mqttConnected"] = False
        if client is None:
            return
        if hasattr(client, "loop_stop"):
            client.loop_stop()
        if hasattr(client, "disconnect"):
            client.disconnect()

    def _button_event(self) -> dict[str, Any]:
        return {
            "eventType": "gpio_button_pressed",
            "source": "gpio_button",
            "pin": self.settings.gpio_button_pin,
            "targetScene": "relax",
            "pressCount": self._status["pressCount"],
            "tsMs": round(time.time() * 1000),
        }

    def _publish_pressed_event(self, event: dict[str, Any]) -> None:
        if self._mqtt_client is None or not self._mqtt_connected:
            self._status["lastError"] = "mqtt_not_connected"
            return
        payload = {"event": event}
        try:
            info = self._mqtt_client.publish(
                self.topics.controller_event,
                json.dumps(payload, ensure_ascii=False),
                qos=1,
            )
        except Exception as error:
            self._status["lastError"] = str(error)
            return
        if getattr(info, "rc", 0) not in (0, None):
            self._status["lastError"] = "mqtt_publish_failed"
            return
        self._status["lastError"] = None

    def _on_mqtt_connect(self, _client: Any, _userdata: Any, _flags: Any, reason_code: Any, _properties: Any = None) -> None:
        connected = reason_code in (0, "0", None) or getattr(reason_code, "value", reason_code) == 0
        self._mqtt_connected = connected
        self._status["mqttConnected"] = connected

    def _on_mqtt_disconnect(self, *_args: Any) -> None:
        self._mqtt_connected = False
        self._status["mqttConnected"] = False

    @staticmethod
    def _load_gpiozero_button() -> Callable[..., Any] | None:
        try:
            from gpiozero import Button
        except Exception:
            return None
        return Button

    @staticmethod
    def _load_mqtt_client() -> Any | None:
        try:
            import paho.mqtt.client as mqtt
        except Exception:
            return None
        try:
            return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="smart_space_gpio_button")
        except Exception:
            return mqtt.Client(client_id="smart_space_gpio_button")
