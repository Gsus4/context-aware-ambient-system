from __future__ import annotations

import asyncio
import json
import time
from collections.abc import Callable
from typing import Any, Protocol

from smart_space.core.payload_utils import as_number_or_none
from smart_space.core.settings import Settings
from smart_space.mqtt.topics import Topics
from smart_space.services.gpio_button import GpioButtonService
from smart_space.system_state import SystemStateManager


SCENE_STATE_TO_CONTROL_SCENE = {
    "EMPTY": "vacant",
    "SLEEPING": "sleep",
    "WORKING": "work",
    "RELAXING": "relax",
    "EXERCISING": "exercise",
}
FAN_BASE_LEVELS = {
    "vacant": 0,
    "sleep": 3,
    "work": 5,
    "relax": 5,
    "exercise": 8,
}
VACANT_CONFIRMATION_COUNT = 2


class Publisher(Protocol):
    def is_connected(self, name: str) -> bool: ...
    def publish(self, name: str, topic: str, payload: str, qos: int = 1) -> None: ...


PublisherProvider = Publisher | Callable[[], Publisher | None] | None
MqttClientFactory = Callable[[], Any | None]
ButtonServiceProvider = GpioButtonService | Callable[[], GpioButtonService | None] | None


class FusionControlService:
    def __init__(
        self,
        settings: Settings,
        system_state: SystemStateManager,
        publisher: PublisherProvider = None,
        *,
        mqtt_client_factory: MqttClientFactory | None = None,
        gpio_button_service: ButtonServiceProvider = None,
    ) -> None:
        self.settings = settings
        self.system_state = system_state
        self.publisher = publisher
        self.mqtt_client_factory = mqtt_client_factory or self._load_mqtt_client
        self.gpio_button_service = gpio_button_service or GpioButtonService(settings, on_pressed=self.handle_gpio_button_pressed)
        self.topics = Topics(settings)
        self._empty_count = 0
        self._mqtt_client: Any | None = None
        self._mqtt_connected = False
        self._loop: asyncio.AbstractEventLoop | None = None
        self._phase = "vacant" if system_state.snapshot().get("currentContext", {}).get("scene") == "vacant" else "occupied"
        self._last_activation_at_ms = 0
        self._last_context_signature: tuple[Any, ...] | None = None

    def start(self, *, loop: asyncio.AbstractEventLoop) -> dict[str, Any]:
        self._loop = loop
        button = self._button_service()
        if button is not None:
            button.start()
        if self._mqtt_client is not None:
            return self.status()
        client = self.mqtt_client_factory()
        if client is None:
            return {"running": False, "mqttConnected": False, "reason": "mqtt_client_unavailable"}
        self._mqtt_client = client
        if self.settings.mqtt_username and hasattr(client, "username_pw_set"):
            client.username_pw_set(self.settings.mqtt_username, self.settings.mqtt_password or None)
        client.on_connect = self._on_mqtt_connect
        client.on_disconnect = self._on_mqtt_disconnect
        client.on_message = self._on_mqtt_message
        try:
            result = client.connect(self.settings.mqtt_host, self.settings.mqtt_port, keepalive=30)
            self._mqtt_connected = result in (0, None)
            if hasattr(client, "loop_start"):
                client.loop_start()
        except Exception as error:
            self._mqtt_client = None
            self._mqtt_connected = False
            return {"running": False, "mqttConnected": False, "reason": str(error)}
        return self.status()

    def stop(self) -> dict[str, Any]:
        button = self._button_service()
        if button is not None:
            button.stop()
        client = self._mqtt_client
        self._mqtt_client = None
        self._mqtt_connected = False
        self._loop = None
        if client is None:
            return self.status()
        if hasattr(client, "loop_stop"):
            client.loop_stop()
        if hasattr(client, "disconnect"):
            client.disconnect()
        return self.status()

    def status(self) -> dict[str, Any]:
        return {
            "running": self._mqtt_client is not None,
            "mqttConnected": self._mqtt_connected,
            "topic": self.topics.controller_event,
            "phase": self._phase,
            "emptyCount": self._empty_count,
            "lastActivationAtMs": self._last_activation_at_ms,
            "gpioButton": self._button_service().status() if self._button_service() else None,
        }

    async def apply_observation(self, observation: dict[str, Any]) -> dict[str, Any]:
        snapshot = self.system_state.snapshot()
        self._sync_cycle_from_snapshot(snapshot)
        if snapshot.get("operationMode", {}).get("smartModeEnabled") is False:
            return {"applied": False, "reason": "smart_mode_disabled"}
        if self._is_stale_observation(observation):
            return {"applied": False, "reason": "stale_observation"}
        if snapshot.get("currentContext", {}).get("scene") == "vacant":
            self._phase = "vacant"
            self._empty_count = 0
            return {"applied": False, "reason": "already_vacant"}

        scene_state = str(observation.get("scene_state") or "UNKNOWN").strip().upper()
        scene = SCENE_STATE_TO_CONTROL_SCENE.get(scene_state)
        if scene is None:
            if scene_state != "EMPTY":
                self._empty_count = 0
            return {"applied": False, "reason": "unsupported_scene_state", "scene_state": scene_state}

        if scene == "vacant":
            self._empty_count += 1
            if self._empty_count < VACANT_CONFIRMATION_COUNT:
                return {
                    "applied": False,
                    "reason": "waiting_for_consecutive_vacant",
                    "emptyCount": self._empty_count,
                }
            if snapshot.get("currentContext", {}).get("scene") == "vacant":
                self._phase = "vacant"
                self._empty_count = 0
                return {"applied": False, "reason": "already_vacant"}
        else:
            self._empty_count = 0
            self._phase = "occupied"

        confidence = as_number_or_none(observation.get("confidence"))
        reason = str(observation.get("reason") or scene_state)
        result = await self.apply_scene(scene, source="fusion_control", reason=reason, confidence=confidence)
        if scene == "vacant" and result.get("applied"):
            self._phase = "vacant"
            self._empty_count = 0
        return result

    async def apply_scene(
        self,
        scene: str,
        *,
        source: str,
        reason: str = "",
        confidence: float | None = None,
        force: bool = False,
    ) -> dict[str, Any]:
        snapshot = self.system_state.snapshot()
        if snapshot.get("operationMode", {}).get("smartModeEnabled") is False and not force:
            return {"applied": False, "reason": "smart_mode_disabled"}
        next_snapshot = self.system_state.set_operation_mode(
            smart_mode_enabled=True,
            control_mode="scene",
            scene=scene,
            source=source,
            confidence=confidence,
            reason=reason,
        )
        published = self._publish_controls(scene, next_snapshot)
        return {"applied": True, "scene": scene, "published": published}

    def handle_gpio_button_pressed(self, event: dict[str, Any] | None = None) -> dict[str, Any]:
        self._empty_count = 0
        self._phase = "occupied"
        self._last_activation_at_ms = _now_ms()
        snapshot = self.system_state.set_operation_mode(
            smart_mode_enabled=True,
            control_mode="scene",
            scene="relax",
            source="gpio_button",
            reason="GPIO button pressed",
        )
        self._last_activation_at_ms = max(self._last_activation_at_ms, int(snapshot.get("updatedAtMs") or 0))
        self._last_context_signature = self._context_signature(snapshot)
        published = self._publish_controls("relax", snapshot)
        return {"applied": True, "scene": "relax", "source": "gpio_button", "event": event or {}, "published": published}

    def _publish_controls(self, scene: str, snapshot: dict[str, Any]) -> list[dict[str, Any]]:
        published: list[dict[str, Any]] = []
        publisher = self._publisher()
        if publisher is None:
            return published

        if publisher.is_connected("light"):
            payload = {"req_id": self._request_id("fusion_light"), "cmd": "set_scene", "params": {"scene": scene}}
            publisher.publish("light", self.topics.smartlight_cmd, json.dumps(payload, ensure_ascii=False), qos=1)
            published.append({"name": "light", "topic": self.topics.smartlight_cmd, "command": "set_scene"})

        fan_level = fan_level_for_scene(scene, snapshot.get("sensors", {}))
        fan_client = self._fan_client(publisher)
        if fan_client:
            mode = "OFF" if fan_level <= 0 else "MANUAL"
            self._publish_fan_command(publisher, fan_client, "set_mode", {"mode": mode})
            self._publish_fan_command(publisher, fan_client, "set_fan_level", {"fan_level": fan_level})
            published.append({"name": fan_client, "topic": self.topics.fan_cmd, "command": "set_fan_level", "fanLevel": fan_level})
        return published

    def _publisher(self) -> Publisher | None:
        if callable(self.publisher) and not hasattr(self.publisher, "publish"):
            return self.publisher()
        return self.publisher

    def _fan_client(self, publisher: Publisher) -> str | None:
        if publisher.is_connected("env"):
            return "env"
        if publisher.is_connected("light"):
            return "light"
        return None

    def _publish_fan_command(self, publisher: Publisher, client_name: str, command: str, params: dict[str, Any]) -> None:
        payload = {
            "command": command,
            "params": params,
            "request_id": self._request_id("fusion_fan"),
            "source": "fusion_control",
        }
        publisher.publish(client_name, self.topics.fan_cmd, json.dumps(payload, ensure_ascii=False), qos=1)

    @staticmethod
    def _request_id(prefix: str) -> str:
        return f"{prefix}_{round(time.time() * 1000)}"

    def _on_mqtt_connect(self, client: Any, _userdata: Any, _flags: Any, reason_code: Any, _properties: Any = None) -> None:
        self._mqtt_connected = reason_code in (0, "0", None) or getattr(reason_code, "value", reason_code) == 0
        if self._mqtt_connected:
            client.subscribe(self.topics.controller_event, qos=1)

    def _on_mqtt_disconnect(self, *_args: Any) -> None:
        self._mqtt_connected = False

    def _on_mqtt_message(self, _client: Any, _userdata: Any, message: Any) -> None:
        if getattr(message, "topic", "") != self.topics.controller_event:
            return
        try:
            raw = message.payload.decode() if isinstance(message.payload, bytes) else str(message.payload)
            data = json.loads(raw)
        except Exception:
            return
        body = data.get("event") if isinstance(data.get("event"), dict) else data
        if not isinstance(body, dict) or body.get("eventType") != "gpio_button_pressed":
            return
        self.handle_gpio_button_pressed(body)

    def _button_service(self) -> GpioButtonService | None:
        if callable(self.gpio_button_service) and not hasattr(self.gpio_button_service, "start"):
            return self.gpio_button_service()
        return self.gpio_button_service

    def _sync_cycle_from_snapshot(self, snapshot: dict[str, Any]) -> None:
        signature = self._context_signature(snapshot)
        if signature == self._last_context_signature:
            return
        self._last_context_signature = signature
        scene = snapshot.get("currentContext", {}).get("scene")
        source = snapshot.get("currentContext", {}).get("source")
        if scene == "vacant":
            self._phase = "vacant"
            self._empty_count = 0
            return
        if source in {"gpio_button", "pir"}:
            self._phase = "occupied"
            self._empty_count = 0
            self._last_activation_at_ms = int(snapshot.get("updatedAtMs") or _now_ms())

    @staticmethod
    def _context_signature(snapshot: dict[str, Any]) -> tuple[Any, ...]:
        context = snapshot.get("currentContext", {})
        operation = snapshot.get("operationMode", {})
        return (
            operation.get("smartModeEnabled"),
            operation.get("controlMode"),
            context.get("scene"),
            context.get("source"),
            context.get("reason"),
        )

    def _is_stale_observation(self, observation: dict[str, Any]) -> bool:
        request_started_at_ms = as_number_or_none(observation.get("request_started_at_ms"))
        if request_started_at_ms is not None and self._last_activation_at_ms and request_started_at_ms < self._last_activation_at_ms:
            return True
        if str(observation.get("request_context_scene") or "").strip().lower() == "vacant":
            return True
        return False

    @staticmethod
    def _load_mqtt_client() -> Any | None:
        try:
            import paho.mqtt.client as mqtt
        except Exception:
            return None
        try:
            return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="smart_space_fusion_control")
        except Exception:
            return mqtt.Client(client_id="smart_space_fusion_control")


def fan_level_for_scene(scene: str, sensors: dict[str, Any]) -> int:
    level = FAN_BASE_LEVELS.get(scene, FAN_BASE_LEVELS["relax"])
    if scene == "vacant":
        return 0

    temperature = as_number_or_none(sensors.get("temperature"))
    humidity = as_number_or_none(sensors.get("humidity"))
    aqi = as_number_or_none(sensors.get("aqi"))
    heart_rate = as_number_or_none(sensors.get("heartRate"))

    if temperature is not None:
        if temperature >= 28:
            level += 1
        if temperature >= 31:
            level += 1
        if temperature < 25:
            level -= 1
        if temperature < 22:
            level -= 1
        if temperature < 18:
            level -= 1
            
    if humidity is not None and humidity >= 70:
        level += 1
    if aqi is not None:
        if aqi >= 150:
            level += 1
        if aqi >= 200:
            level += 1
    if heart_rate is not None:
        if heart_rate >= 100:
            level += 1
        if heart_rate >= 120:
            level += 1
    return max(0, min(10, int(level)))


def _now_ms() -> int:
    return round(time.time() * 1000)
