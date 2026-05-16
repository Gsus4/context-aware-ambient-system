from __future__ import annotations

import json
import time
from collections.abc import Callable
from typing import Any, Protocol

from smart_space.core.settings import Settings
from smart_space.core.payload_utils import (
    as_number_or_none,
    get_field,
    get_field_including_null,
    hex_to_rgb,
    kelvin_to_hex,
    normalize_online_from_payload,
    normalize_timestamp_fields,
    payload_body,
    safe_int,
    truthy_state,
)
from smart_space.mqtt.topics import Topics
from smart_space.system_state import SystemStateManager


class Publisher(Protocol):
    def is_connected(self, name: str) -> bool: ...
    def publish(self, name: str, topic: str, payload: str, qos: int = 1) -> None: ...


WEB_MODE_DEFAULTS = {
    "vacant": {"label": "無人", "scene": "vacant", "brightness": 0, "kelvin": 2700, "brightnessMin": 0, "brightnessMax": 0, "kelvinMin": 2700, "kelvinMax": 6500},
    "sleep": {"label": "睡眠", "scene": "sleep", "brightness": 30, "kelvin": 2700, "brightnessMin": 0, "brightnessMax": 30, "kelvinMin": 2700, "kelvinMax": 6500},
    "relax": {"label": "放鬆", "scene": "relax", "brightness": 40, "kelvin": 3000, "brightnessMin": 5, "brightnessMax": 70, "kelvinMin": 2700, "kelvinMax": 6500},
    "work": {"label": "工作", "scene": "work", "brightness": 60, "kelvin": 4500, "brightnessMin": 0, "brightnessMax": 100, "kelvinMin": 2700, "kelvinMax": 6500},
    "exercise": {"label": "運動", "scene": "exercise", "brightness": 60, "kelvin": 3000, "brightnessMin": 10, "brightnessMax": 85, "kelvinMin": 2700, "kelvinMax": 6500},
    "custom": {"label": "自定義模式", "scene": None, "brightness": 64, "kelvin": 3900, "brightnessMin": 0, "brightnessMax": 100, "kelvinMin": 2700, "kelvinMax": 6500},
}

SMART_SCENE_TO_WEB_MODE = {
    "vacant": "vacant",
    "sleep": "sleep",
    "relax": "relax",
    "work": "work",
    "exercise": "exercise",
    "none": None,
}

ALLOWED_WEB_ACTIONS = {
    "get_status",
    "set_mode",
    "set_auto",
    "set_smart_mode",
    "set_led_power",
    "set_brightness",
    "set_kelvin",
    "set_color",
    "set_advanced_light",
    "set_custom_light",
    "apply_current_light",
    "set_fan_level",
    "set_fan_mode",
}

MODULE_DEFS = [
    {"key": "led", "label": "LED系統"},
    {"key": "wearable", "label": "穿戴式手環"},
    {"key": "envComfort", "label": "環境舒適度/節能控制節點"},
    {"key": "audio", "label": "音樂播放服務"},
    {"key": "camera", "label": "空間影像串流"},
]

ERROR_TEXT = {
    "unsupported_dashboard_action": "Dashboard 不支援此操作",
    "invalid_fan_level": "風扇等級需為 0–10",
    "invalid_fan_mode": "風扇模式需為 OFF / MANUAL / AUTO",
    "mqtt_not_connected": "MQTT 尚未連線，請稍後再試",
}


class DashboardState:
    def __init__(self, settings: Settings, publisher: Publisher | None = None, system_state: SystemStateManager | None = None):
        self.settings = settings
        self.topics = Topics(settings)
        self.publisher = publisher
        self.system_state = system_state
        self.availability = "unknown"
        self.current_web_mode = "work"
        self.last_req_seq = 0
        self.last_smartlight_status_at = 0.0
        self.module_last_seen = {item["key"]: {"at": 0.0, "detail": ""} for item in MODULE_DEFS}
        self.pending_commands: dict[str, dict[str, Any]] = {}
        self.smart_control_enabled = settings.smart_mode_enabled
        self.listeners: list[Callable[[dict[str, Any]], None]] = []
        self.last_payload = {
            "uiState": self.smart_control_snapshot(),
            "sensorData": {
                "heartRate": None,
                "spo2": None,
                "aqi": None,
                "temperature": None,
                "humidity": None,
                "comfort": None,
                "pirDetected": None,
                "pirTime": None,
                "onlineDevices": 0,
                "totalDevices": len(MODULE_DEFS),
                "systemStatus": "等待實機資料",
                "moduleStatus": [],
                "deviceConnectionText": "",
                "cameraSnapshotUrl": None,
                "cameraHlsUrl": None,
                "cameraWebrtcUrl": None,
                "cameraWhepUrl": None,
                "cameraRtspUrl": None,
                "cameraStreamPath": None,
            },
            "deviceState": {
                "ledOn": None,
                "brightness": None,
                "kelvin": None,
                "ledColor": "#2d3748",
                "fanOn": None,
                "fanSpeed": None,
            },
            "audioState": {
                "connected": None,
                "playing": False,
                "state": "STOPPED",
                "mode": None,
                "scene": None,
                "autoModeEnabled": True,
                "volume": None,
                "track": None,
                "trackPath": None,
                "outputDevice": None,
                "progressSec": None,
                "durationSec": None,
            },
            "mode": "work",
            "sourceStatus": "等待實機資料",
            "connectionState": {
                "websocket": True,
                "mqtt": False,
                "smartLight": "unknown",
                "totalDevices": len(MODULE_DEFS),
                "onlineDevices": 0,
                "moduleStatus": [],
            },
        }

    def add_listener(self, listener: Callable[[dict[str, Any]], None]) -> None:
        self.listeners.append(listener)

    def remove_listener(self, listener: Callable[[dict[str, Any]], None]) -> None:
        if listener in self.listeners:
            self.listeners.remove(listener)

    def now_req_id(self, prefix: str = "web") -> str:
        self.last_req_seq += 1
        return f"{prefix}_{round(time.time() * 1000)}_{self.last_req_seq:04d}"

    def smart_control_snapshot(self) -> dict[str, Any]:
        return {
            "smartModeEnabled": self.smart_control_enabled,
            "smartModeLabel": "智慧模式開啟" if self.smart_control_enabled else "智慧模式關閉",
        }

    def mark_module(self, key: str, detail: str = "") -> None:
        alias = self.module_alias(key)
        if alias in self.module_last_seen:
            self.module_last_seen[alias] = {"at": time.time() * 1000, "detail": detail or self.module_last_seen[alias]["detail"]}

    def set_module_online(self, key: str, online: bool, detail: str = "") -> None:
        alias = self.module_alias(key)
        if alias in self.module_last_seen:
            self.module_last_seen[alias] = {"at": time.time() * 1000 if online else 0.0, "detail": detail or self.module_last_seen[alias]["detail"]}

    @staticmethod
    def module_alias(key: str) -> str:
        if key in {"heart", "spo2", "wearable"}:
            return "wearable"
        if key in {"aqi", "tempHumidity", "pir", "fan", "env", "envComfort"}:
            return "envComfort"
        if key in {"audio", "music"}:
            return "audio"
        return key

    def module_snapshot(self) -> list[dict[str, Any]]:
        now_ms = time.time() * 1000
        modules = []
        for item in MODULE_DEFS:
            seen = self.module_last_seen[item["key"]]
            online = bool(seen["at"] and now_ms - seen["at"] <= self.settings.dashboard_module_stale_ms)
            if item["key"] == "audio" and self.last_payload.get("audioState", {}).get("connected") is True:
                online = True
            if item["key"] == "camera" and (
                self.last_payload.get("sensorData", {}).get("cameraWebrtcUrl")
                or self.last_payload.get("sensorData", {}).get("cameraHlsUrl")
            ):
                online = True
            modules.append({
                "key": item["key"],
                "label": item["label"],
                "online": online,
                "status": "online" if online else "offline",
                "lastSeenAt": seen["at"] or None,
                "detail": seen["detail"] if online else "--",
            })
        return modules

    def current_connection_state(self) -> dict[str, Any]:
        modules = self.module_snapshot()
        online = len([item for item in modules if item["online"]])
        total = len(modules)
        system_status = "正常運作" if online == total else "部分連線" if online > 0 else "等待實機資料"
        return {
            "websocket": True,
            "mqtt": self._publisher_connected("light"),
            "smartLight": self.availability,
            "lightMqtt": self._publisher_connected("light"),
            "wearableMqtt": self._publisher_connected("wearable") or self._publisher_connected("light"),
            "envMqtt": self._publisher_connected("env") or self._publisher_connected("light"),
            "onlineDevices": online,
            "totalDevices": total,
            "systemStatus": system_status,
            "deviceConnections": modules,
            "moduleStatus": modules,
            "deviceConnectionText": "｜".join(f"{item['label']}：{'在線' if item['online'] else '未連線'}" for item in modules),
        }

    def snapshot(self) -> dict[str, Any]:
        return self._merge_payload({})

    def broadcast_patch(self, patch: dict[str, Any]) -> dict[str, Any]:
        payload = self._merge_payload(patch)
        for listener in list(self.listeners):
            listener(payload)
        return payload

    def _merge_payload(self, patch: dict[str, Any]) -> dict[str, Any]:
        summary = self.current_connection_state()
        state_patch = dict(patch)
        state_patch.setdefault("sensorData", {})
        state_patch.setdefault("connectionState", {})
        state_patch.setdefault("uiState", {})
        state_patch["sensorData"] = {
            **state_patch["sensorData"],
            "onlineDevices": summary["onlineDevices"],
            "totalDevices": summary["totalDevices"],
            "systemStatus": summary["systemStatus"],
            "moduleStatus": summary["moduleStatus"],
            "deviceConnectionText": summary["deviceConnectionText"],
        }
        state_patch["connectionState"] = {**summary, **state_patch["connectionState"]}
        state_patch["uiState"] = {**self.smart_control_snapshot(), **state_patch["uiState"]}

        user_event = state_patch.pop("userEvent", None)
        debug_event = state_patch.pop("debugEvent", None)
        self.last_payload = {
            **self.last_payload,
            **state_patch,
            "sensorData": {**self.last_payload["sensorData"], **state_patch.get("sensorData", {})},
            "deviceState": {**self.last_payload["deviceState"], **state_patch.get("deviceState", {})},
            "audioState": {**self.last_payload["audioState"], **state_patch.get("audioState", {})},
            "uiState": {**self.last_payload["uiState"], **state_patch.get("uiState", {})},
            "connectionState": {**self.last_payload["connectionState"], **state_patch.get("connectionState", {})},
        }
        return {
            **self.last_payload,
            **({"userEvent": user_event} if user_event else {}),
            **({"debugEvent": debug_event} if debug_event else {}),
        }

    def handle_dashboard_command(self, message: dict[str, Any]) -> dict[str, Any] | list[dict[str, Any]] | None:
        action = message.get("action")
        if action not in ALLOWED_WEB_ACTIONS:
            raise ValueError("unsupported_dashboard_action")

        current_device = self.last_payload.get("deviceState", {})
        requested_mode = message.get("mode") or self.current_web_mode or "work"
        mode = requested_mode if requested_mode in WEB_MODE_DEFAULTS else "work"
        defaults = WEB_MODE_DEFAULTS[mode]
        manual = message.get("controlMode") == "manual"
        brightness = safe_int(
            message.get("brightness", current_device.get("brightness", defaults["brightness"])),
            defaults.get("brightnessMin", 0),
            defaults.get("brightnessMax", 100),
            defaults["brightness"],
        )
        kelvin = safe_int(
            message.get("kelvin", current_device.get("kelvin", defaults["kelvin"])),
            defaults.get("kelvinMin", 2700),
            defaults.get("kelvinMax", 6500),
            defaults["kelvin"],
        )

        if action == "get_status":
            return self.publish_smartlight_command("get_status", {}, self.now_req_id("web_get_status"), {"action": "get_status", "silent": message.get("silent") is True})

        if action == "set_smart_mode":
            self.smart_control_enabled = message.get("enabled") is not False
            control_mode = message.get("controlMode")
            resolved_control_mode = control_mode or ("scene" if self.smart_control_enabled else self.last_payload["uiState"].get("controlMode", "scene"))
            self._sync_system_operation(smart_mode_enabled=self.smart_control_enabled, control_mode=resolved_control_mode)
            self.broadcast_patch({
                "uiState": {"controlMode": resolved_control_mode},
                "sourceStatus": "智慧模式已開啟" if self.smart_control_enabled else "智慧模式已關閉",
            })
            return {"ok": True, "smartModeEnabled": self.smart_control_enabled}

        if action == "set_auto":
            self.current_web_mode = "work"
            result = self.publish_smartlight_command("set_auto", {"enabled": True}, self.now_req_id("web_auto"), {"action": "set_auto"})
            self._sync_system_operation(control_mode="auto", scene="work")
            return result

        if action == "set_mode":
            command, params = self.scene_command_for_mode(mode, brightness, kelvin)
            self.current_web_mode = mode
            result = self.publish_smartlight_command(command, params, self.now_req_id(f"web_{mode}"), {"action": "set_mode", "mode": mode})
            self._sync_system_operation(control_mode="scene", scene=mode)
            return result

        if action == "set_led_power":
            if message.get("on") is False:
                return self.publish_smartlight_command("power_off", {}, self.now_req_id("web_power_off"), {"action": "set_led_power", "on": False})
            restore_mode = "custom" if manual else mode
            command, params = self.scene_command_for_mode(restore_mode, brightness or WEB_MODE_DEFAULTS[restore_mode]["brightness"], kelvin, manual)
            self.current_web_mode = restore_mode
            result = self.publish_smartlight_command(command, params, self.now_req_id("web_power_on"), {"action": "set_led_power", "on": True, "mode": restore_mode})
            self._sync_system_operation(control_mode="manual" if manual else "scene", scene=restore_mode)
            return result

        if action in {"set_brightness", "set_kelvin", "apply_current_light"}:
            force_static = manual or mode == "custom" or action == "apply_current_light"
            command_mode = "custom" if force_static else mode
            command, params = self.scene_command_for_mode(command_mode, brightness, kelvin, force_static)
            self.current_web_mode = command_mode
            result = self.publish_smartlight_command(command, params, self.now_req_id("web_apply" if action == "apply_current_light" else f"web_{action.removeprefix('set_')}"), {"action": action, "mode": command_mode, "brightness": brightness, "kelvin": kelvin})
            self._sync_system_operation(control_mode="manual" if force_static else "scene", scene=command_mode)
            return result

        if action == "set_color":
            rgb = hex_to_rgb(message.get("color") or message.get("ledColor"))
            if not rgb:
                raise ValueError("invalid_color")
            return self.publish_smartlight_command("set_static", {
                "brightness_pct": brightness or WEB_MODE_DEFAULTS["custom"]["brightness"],
                "rgb": rgb,
            }, self.now_req_id("web_color"), {"action": "set_color"})

        if action == "set_advanced_light":
            return self._handle_advanced_light(message, brightness, kelvin)

        if action == "set_custom_light":
            return self._handle_custom_light(message, brightness)

        if action == "set_fan_level":
            fan_level = safe_int(message.get("fanLevel", message.get("fan_level", message.get("fanSpeed", message.get("fan_speed")))), 0, 10, 0)
            self.broadcast_patch({
                "deviceState": {
                    "fanSpeed": fan_level,
                    "fanOn": fan_level > 0,
                },
                "sourceStatus": "等待風扇模組回應",
            })
            return self.publish_fan_command("set_fan_level", {"fan_level": fan_level}, self.now_req_id("web_fan_level"), {"action": "set_fan_level", "fanLevel": fan_level})

        if action == "set_fan_mode":
            fan_mode = str(message.get("mode") or message.get("fanMode") or "").strip().upper()
            if fan_mode not in {"OFF", "MANUAL", "AUTO"}:
                raise ValueError("invalid_fan_mode")
            return self.publish_fan_command("set_mode", {"mode": fan_mode}, self.now_req_id("web_fan_mode"), {"action": "set_fan_mode", "mode": fan_mode})

        return None

    def scene_command_for_mode(self, mode: str, brightness: int, kelvin: int, force_static: bool = False) -> tuple[str, dict[str, Any]]:
        defaults = WEB_MODE_DEFAULTS.get(mode, WEB_MODE_DEFAULTS["work"])
        scene = None if force_static else defaults.get("scene")
        final_brightness = safe_int(brightness, defaults.get("brightnessMin", 0), defaults.get("brightnessMax", 100), defaults["brightness"])
        final_kelvin = safe_int(kelvin, defaults.get("kelvinMin", 2700), defaults.get("kelvinMax", 6500), defaults["kelvin"])
        if not scene:
            return "set_static", {"brightness_pct": final_brightness, "color_temp_k": final_kelvin}
        return "set_scene", {"scene": scene}

    def publish_smartlight_command(self, command: str, params: dict[str, Any], req_id: str, meta: dict[str, Any]) -> dict[str, Any]:
        if not self._publisher_connected("light"):
            raise ValueError("mqtt_not_connected")
        payload = {"req_id": req_id or self.now_req_id("web"), "cmd": command}
        if params:
            payload["params"] = params
        self.pending_commands[payload["req_id"]] = {"action": meta.get("action", command), "command": command, "params": params, "meta": meta, "sentAt": round(time.time() * 1000)}
        self.publisher.publish("light", self.topics.smartlight_cmd, json.dumps(payload, ensure_ascii=False), qos=1)
        self.broadcast_patch({"sourceStatus": "等待 SmartLight 回應"})
        return payload

    def publish_integration_command(self, topic: str, command: str, params: dict[str, Any], req_id: str, meta: dict[str, Any]) -> dict[str, Any]:
        client_name = self.integration_client_name_for_topic(topic)
        if not self._publisher_connected(client_name):
            raise ValueError("mqtt_not_connected")
        payload = {"command": command, "params": params or {}, "request_id": req_id or self.now_req_id("web_integration"), "source": "server"}
        self.pending_commands[payload["request_id"]] = {"action": meta.get("action", command), "command": command, "params": params, "meta": {**meta, "integrationTopic": topic}, "sentAt": round(time.time() * 1000)}
        self.publisher.publish(client_name, topic, json.dumps(payload, ensure_ascii=False), qos=1)
        self.broadcast_patch({"sourceStatus": "等待模組回應"})
        return payload

    def publish_fan_command(self, command: str, params: dict[str, Any], req_id: str, meta: dict[str, Any]) -> dict[str, Any]:
        return self.publish_integration_command(self.topics.fan_cmd, command, params, req_id, {**meta, "module": "fan"})

    def integration_client_name_for_topic(self, topic: str) -> str:
        parts = self.topics.integration_parts(topic)
        if parts and parts["domain"] == "env":
            return "env" if self._publisher_connected("env") else "light"
        if parts and parts["domain"] == "wearable":
            return "wearable" if self._publisher_connected("wearable") else "light"
        return "light"

    def _publisher_connected(self, name: str) -> bool:
        return bool(self.publisher and self.publisher.is_connected(name))

    def _sync_system_operation(
        self,
        *,
        smart_mode_enabled: bool | None = None,
        control_mode: str | None = None,
        scene: str | None = None,
    ) -> None:
        if not self.system_state:
            return
        self.system_state.set_operation_mode(
            smart_mode_enabled=smart_mode_enabled,
            control_mode=control_mode,
            scene=scene,
            source="dashboard",
        )

    def _handle_advanced_light(self, message: dict[str, Any], brightness: int, kelvin: int) -> dict[str, Any] | list[dict[str, Any]]:
        effect = str(message.get("effect") or "static").strip()
        if effect == "flow" or message.get("flowEnabled") is True:
            return self.publish_smartlight_command("set_flow", {
                "preset": message.get("flowPreset") or message.get("flow_preset") or "soft_rainbow",
                "speed": message.get("flowSpeed") or message.get("flow_speed") or "medium",
                "brightness_pct": safe_int(message.get("flowBrightness", message.get("brightness", brightness)), 0, 100, WEB_MODE_DEFAULTS["custom"]["brightness"]),
            }, self.now_req_id("web_flow"), {"action": "set_advanced_light"})
        rgb = hex_to_rgb(message.get("color") or message.get("ledColor"))
        params = {"brightness_pct": safe_int(message.get("brightness", brightness), 0, 100, WEB_MODE_DEFAULTS["custom"]["brightness"])}
        if rgb:
            params["rgb"] = rgb
        else:
            params["color_temp_k"] = kelvin
        published = [self.publish_smartlight_command("set_static", params, self.now_req_id("web_advanced_static"), {"action": "set_advanced_light"})]
        if message.get("breathingEnabled") is True:
            published.append(self.publish_smartlight_command("set_static_breathing", {
                "enabled": True,
                "speed": message.get("breathingSpeed") or "slow",
                "strength": safe_int(message.get("breathingStrength", message.get("breathing_strength")), 0, 100, 50),
            }, self.now_req_id("web_advanced_breathing"), {"action": "set_advanced_light"}))
        return published

    def _handle_custom_light(self, message: dict[str, Any], brightness: int) -> dict[str, Any] | list[dict[str, Any]]:
        custom_control_type = str(message.get("custom_control_type") or message.get("customControlType") or "fixed_brightness").strip()
        color_mode = str(message.get("color_mode") or message.get("colorMode") or "cct").strip()
        rgb = self._message_rgb(message)
        base_params = {
            "min_output": safe_int(message.get("min_output", message.get("minOutput")), 0, 100, 0),
            "max_output": safe_int(message.get("max_output", message.get("maxOutput")), 0, 100, 100),
            "color_mode": color_mode,
            "effect": str(message.get("effect") or "static").strip(),
        }

        if custom_control_type == "target_lux_range":
            target_lux = safe_int(message.get("target_lux", message.get("targetLux")), 0, 100000, 500)
            tolerance_lux = safe_int(message.get("tolerance_lux", message.get("toleranceLux")), 0, 100000, 75)
            if color_mode == "rgb" and rgb:
                return self.publish_smartlight_command("set_custom", {
                    "custom_control_type": "fixed_brightness",
                    "fixed_brightness_pct": safe_int(
                        message.get("fixed_brightness_pct", message.get("brightness")),
                        0,
                        100,
                        WEB_MODE_DEFAULTS["custom"]["brightness"],
                    ),
                    "target_lux": None,
                    "tolerance_lux": None,
                    "target_lux_min": None,
                    "target_lux_max": None,
                    "min_output": base_params["min_output"],
                    "max_output": base_params["max_output"],
                    "color_mode": "rgb",
                    "rgb": rgb,
                    "effect": base_params["effect"],
                }, self.now_req_id("web_custom_target_rgb"), {"action": "set_custom_light"})
            params = {
                "custom_control_type": "target_lux_range",
                "fixed_brightness_pct": None,
                "target_lux": target_lux,
                "tolerance_lux": tolerance_lux,
                "target_lux_min": max(0, round(target_lux - tolerance_lux)),
                "target_lux_max": round(target_lux + tolerance_lux),
                **base_params,
            }
            if rgb:
                params["rgb"] = rgb
            else:
                params["color_temp_k"] = safe_int(message.get("color_temp_k", message.get("colorTempK")), 2700, 6500, 4000)
            return self.publish_smartlight_command("set_custom", params, self.now_req_id("web_custom_target"), {"action": "set_custom_light"})

        fixed_brightness = safe_int(message.get("fixed_brightness_pct", message.get("brightness", brightness)), 0, 100, WEB_MODE_DEFAULTS["custom"]["brightness"])
        if base_params["effect"] == "flow":
            return self.publish_smartlight_command("set_flow", {
                "preset": message.get("flow_preset") or message.get("flowPreset") or "soft_rainbow",
                "speed": message.get("flow_speed") or message.get("flowSpeed") or "medium",
                "brightness_pct": safe_int(message.get("flow_brightness", message.get("flowBrightness", fixed_brightness)), 0, 100, fixed_brightness),
            }, self.now_req_id("web_custom_flow"), {"action": "set_custom_light"})
        params = {"brightness_pct": fixed_brightness}
        if color_mode == "rgb" and rgb:
            params["rgb"] = rgb
        else:
            params["color_temp_k"] = safe_int(message.get("color_temp_k", message.get("colorTempK")), 2700, 6500, WEB_MODE_DEFAULTS["custom"]["kelvin"])
        return self.publish_smartlight_command("set_static", params, self.now_req_id("web_custom_fixed_static"), {"action": "set_custom_light"})

    def _message_rgb(self, message: dict[str, Any]) -> dict[str, int] | None:
        if all(key in message for key in ("r", "g", "b")):
            return {key: safe_int(message[key], 0, 255, 0) for key in ("r", "g", "b")}
        if isinstance(message.get("rgb"), dict):
            return {key: safe_int(message["rgb"].get(key), 0, 255, 0) for key in ("r", "g", "b")}
        return hex_to_rgb(message.get("color") or message.get("ledColor"))

    def handle_mqtt_message(self, topic: str, raw: str | bytes) -> dict[str, Any] | None:
        if self.system_state:
            self.system_state.apply_mqtt_message(topic, raw)

        raw_text = raw.decode() if isinstance(raw, bytes) else str(raw)
        try:
            data = json.loads(raw_text)
        except json.JSONDecodeError:
            data = {"value": raw_text}
        parts = self.topics.integration_parts(topic)

        if topic.endswith("/availability"):
            return self._handle_availability(topic, raw_text, data)

        if topic.endswith("/ack"):
            return self._handle_ack(data)

        if topic.endswith("/event"):
            patch = self.map_generic_module_payload(topic, data)
            return self.broadcast_patch({**patch, "debugEvent": {"time": round(time.time() * 1000), "title": "MQTT event", "detail": {"topic": topic, "payload": data}}})

        if topic.endswith("/status"):
            body = normalize_timestamp_fields(payload_body(data))
            is_legacy_light = topic.startswith(f"{self.topics.base_topic}/")
            is_integration_light = bool(parts and parts["domain"] == "light")
            light_patch = self.map_status_to_dashboard(body) if is_legacy_light or is_integration_light else {}
            generic_patch = self.map_generic_module_payload(topic, data)
            patch = {**light_patch, **generic_patch, "sourceStatus": self.mqtt_source_status()}
            if is_legacy_light or is_integration_light:
                self.last_smartlight_status_at = time.time() * 1000
                self.mark_module("led", f"SmartLight NODE {self.settings.smartlight_node_id} status")
            return self.broadcast_patch(patch)

        generic_patch = self.map_generic_module_payload(topic, data)
        if generic_patch:
            return self.broadcast_patch(generic_patch)
        return None

    def _handle_availability(self, topic: str, raw: str, data: dict[str, Any]) -> dict[str, Any]:
        parts = self.topics.integration_parts(topic)
        online = normalize_online_from_payload(raw, data)
        if parts:
            self._mark_availability_modules(parts, online, f"{parts['domain']}/{parts['node_id']} availability")
            return self.broadcast_patch({"sensorData": self._offline_sensor_patch(parts, online), "sourceStatus": self.mqtt_source_status()})
        self.availability = "online" if online else "offline"
        self.set_module_online("led", online, f"SmartLight NODE {self.settings.smartlight_node_id} availability")
        return self.broadcast_patch({"sourceStatus": self.mqtt_source_status()})

    def _handle_ack(self, data: dict[str, Any]) -> dict[str, Any]:
        body = payload_body(data)
        req_id = body.get("req_id") or body.get("request_id")
        pending = self.pending_commands.pop(req_id, None) if req_id else None
        command = body.get("command") or body.get("cmd") or (pending or {}).get("command")
        return self.broadcast_patch({
            "debugEvent": {"time": round(time.time() * 1000), "title": "MQTT ack", "detail": body},
            "sourceStatus": f"{command or '指令'} 已回應",
        })

    def _mark_availability_modules(self, parts: dict[str, str], online: bool, detail: str) -> None:
        if parts["domain"] == "wearable":
            self.set_module_online("wearable", online, detail)
        elif parts["domain"] == "env":
            self.set_module_online("envComfort", online, detail)
        elif parts["domain"] == "light":
            self.set_module_online("led", online, detail)

    def _offline_sensor_patch(self, parts: dict[str, str], online: bool) -> dict[str, Any]:
        if online:
            return {}
        if parts["domain"] == "env" and parts["node_id"] == self.settings.env_sensor_node_id:
            return {"temperature": None, "humidity": None, "aqi": None, "pirDetected": None, "pirTime": None}
        return {}

    def mqtt_source_status(self) -> str:
        connected = len([name for name in ("light", "env", "wearable") if self._publisher_connected(name)])
        if self.availability == "online":
            return f"SmartLight 已連線｜MQTT {connected}/3"
        if self.availability == "offline":
            return f"SmartLight 離線｜MQTT {connected}/3"
        if self._publisher_connected("light"):
            return f"燈光 MQTT 已連線，等待 SmartLight｜MQTT {connected}/3"
        return f"MQTT 連線中｜MQTT {connected}/3"

    def map_status_to_dashboard(self, status: dict[str, Any]) -> dict[str, Any]:
        active_scene = status.get("active_scene") or "none"
        mode = SMART_SCENE_TO_WEB_MODE.get(active_scene) or self.current_web_mode or "work"
        brightness = as_number_or_none(get_field(status, ["brightness_pct", "brightness", "output_percent", "led_output_percent"]))
        kelvin = as_number_or_none(get_field(status, ["color_temp_k", "kelvin"]))
        current_lux = as_number_or_none(get_field(status, ["current_lux", "ambient_lux", "lux"]))
        target_lux = as_number_or_none(get_field_including_null(status, ["target_lux"]))
        tolerance_lux = as_number_or_none(get_field_including_null(status, ["tolerance_lux"]))
        led_on_raw = truthy_state(get_field(status, ["led_on", "ledOn", "power", "enabled"]))
        flow_enabled = status.get("flow_enabled") is True
        led_on = led_on_raw if led_on_raw is not None else (brightness is not None and (brightness > 0 or flow_enabled))
        device_state = {
            "ledOn": led_on,
            "brightness": brightness,
            "kelvin": kelvin,
            "ledColor": kelvin_to_hex(kelvin or WEB_MODE_DEFAULTS.get(mode, WEB_MODE_DEFAULTS["work"])["kelvin"]),
            "activeScene": active_scene,
            "activeMode": mode,
            "currentLux": current_lux,
            "targetLux": target_lux,
            "toleranceLux": tolerance_lux,
            "sensorOk": truthy_state(get_field(status, ["sensor_ok", "lux_sensor_ok", "light_sensor_ok"])),
            "controlState": get_field(status, ["control_state"]),
            "adaptiveProfile": get_field(status, ["adaptive_profile"]),
        }
        fan_raw = get_field(status, ["fan_speed", "fanSpeed", "fan_level", "fanLevel"])
        fan_on_raw = get_field(status, ["fan_on", "fanOn", "fan_enabled", "fanEnabled"])
        fan_speed = as_number_or_none(fan_raw)
        fan_on = truthy_state(fan_on_raw)
        if fan_speed is not None:
            device_state["fanSpeed"] = fan_speed
            device_state["fanOn"] = fan_on if fan_on is not None else fan_speed > 0
        elif fan_on is not None:
            device_state["fanOn"] = fan_on
        sensor_data = {
            "currentLux": current_lux,
            "targetLux": target_lux,
            "toleranceLux": tolerance_lux,
            "ledOutputPercent": brightness,
        }
        return {"deviceState": device_state, "sensorData": sensor_data, "mode": mode}

    def map_generic_module_payload(self, topic: str, data: dict[str, Any]) -> dict[str, Any]:
        body = payload_body(data)
        parts = self.topics.integration_parts(topic)
        sensor_data: dict[str, Any] = {}
        device_state: dict[str, Any] = {}
        audio_state: dict[str, Any] = {}

        heart = as_number_or_none(get_field(body, ["heartRate", "heart_rate", "bpm", "hr", "heart"]))
        if heart is not None:
            sensor_data["heartRate"] = heart
            self.mark_module("wearable", "wearable status")
        spo2 = as_number_or_none(get_field(body, ["spo2", "SpO2", "blood_oxygen", "bloodOxygen", "oxygen"]))
        if spo2 is not None:
            sensor_data["spo2"] = spo2
            self.mark_module("wearable", "wearable status")
        aqi_raw = get_field_including_null(body, ["aqi", "AQI", "air_quality", "airQuality", "air_quality_index", "air_raw", "pm25", "pm2_5"])
        if aqi_raw is None and "aqi" in body:
            sensor_data["aqi"] = None
        else:
            aqi = as_number_or_none(aqi_raw)
            if aqi is not None:
                sensor_data["aqi"] = aqi
                self.mark_module("envComfort", "env status")
        temp_raw = get_field_including_null(body, ["temperature", "temp", "temperature_c", "temperatureC"])
        humidity_raw = get_field_including_null(body, ["humidity", "humid", "humidity_percent", "rh"])
        temp = as_number_or_none(temp_raw)
        humidity = as_number_or_none(humidity_raw)
        if temp is not None:
            sensor_data["temperature"] = temp
            self.mark_module("envComfort", "env status")
        if humidity is not None:
            sensor_data["humidity"] = humidity
            self.mark_module("envComfort", "env status")
        pir_raw = get_field_including_null(body, ["pir", "motion", "presence", "occupancy", "human", "humanDetected", "detected", "value"])
        pir = None if pir_raw is None else truthy_state(pir_raw)
        if pir is not None:
            sensor_data["pirDetected"] = pir
            sensor_data["pirTime"] = time.strftime("%H:%M:%S")
            self.mark_module("envComfort", "env status")
        comfort = get_field(body, ["comfort", "comfort_score", "comfortScore", "comfort_label", "comfortLabel"])
        if comfort is not None:
            normalized_comfort = normalize_comfort_value(comfort)
            sensor_data.update(normalized_comfort)
            self.mark_module("envComfort", "env status")
        fan_speed = as_number_or_none(get_field(body, ["fanSpeed", "fan_speed", "fanLevel", "fan_level", "speed"]))
        fan_on = truthy_state(get_field(body, ["fanOn", "fan_on", "fanEnabled", "fan_enabled", "enabled"]))
        if fan_speed is not None:
            device_state["fanSpeed"] = fan_speed
            device_state["fanOn"] = fan_on if fan_on is not None else fan_speed > 0
            self.mark_module("envComfort", "fan status")
        audio_connected = truthy_state(get_field(body, ["audioConnected", "bluetoothConnected", "connected", "online"]))
        if parts and parts["domain"] == "audio" and audio_connected is not None:
            audio_state["connected"] = audio_connected
            self.mark_module("audio", "audio status")
        for output_key, names in {
            "playing": ["playing", "isPlaying"],
            "state": ["state", "playbackState"],
            "volume": ["volume", "volume_pct", "volumePct"],
            "track": ["track", "title", "song"],
            "trackPath": ["trackPath", "track_path", "path"],
            "outputDevice": ["outputDevice", "output_device", "sink"],
            "mode": ["mode", "audioMode"],
            "scene": ["scene"],
            "autoModeEnabled": ["autoModeEnabled", "auto_mode_enabled"],
            "progressSec": ["progressSec", "progress_sec", "positionSec", "position"],
            "durationSec": ["durationSec", "duration_sec", "duration"],
        }.items():
            value = get_field(body, names)
            if value is None:
                continue
            audio_state[output_key] = truthy_state(value) if output_key == "playing" else as_number_or_none(value) if output_key in {"volume", "progressSec", "durationSec"} else value
            if parts and parts["domain"] == "audio":
                self.mark_module("audio", "audio status")

        camera_online = truthy_state(get_field(body, ["cameraOnline", "camera_connected", "cameraConnected", "online", "connected"]))
        snapshot_url = get_field(body, ["snapshotUrl", "snapshot_url", "imageUrl", "image_url"])
        hls_url = get_field(body, ["hlsUrl", "hls_url"])
        webrtc_url = get_field(body, ["webrtcUrl", "webrtc_url", "webRtcUrl", "web_rtc_url"])
        whep_url = get_field(body, ["whepUrl", "whep_url"])
        rtsp_url = get_field(body, ["rtspUrl", "rtsp_url", "streamUrl", "stream_url"])
        stream_path = get_field(body, ["streamPath", "stream_path"])
        if parts and parts["domain"] in {"camera", "vision"} and camera_online is not None:
            self.set_module_online("camera", camera_online, "camera status")
        if snapshot_url:
            sensor_data["cameraSnapshotUrl"] = snapshot_url
        if hls_url:
            sensor_data["cameraHlsUrl"] = hls_url
        if webrtc_url:
            sensor_data["cameraWebrtcUrl"] = webrtc_url
        if whep_url:
            sensor_data["cameraWhepUrl"] = whep_url
        if rtsp_url:
            sensor_data["cameraRtspUrl"] = rtsp_url
        if stream_path:
            sensor_data["cameraStreamPath"] = stream_path

        patch: dict[str, Any] = {}
        if sensor_data:
            patch["sensorData"] = sensor_data
        if device_state:
            patch["deviceState"] = device_state
        if audio_state:
            patch["audioState"] = audio_state
        return patch


def readable_error(reason: str) -> str:
    return ERROR_TEXT.get(reason, reason or "未知錯誤")


def normalize_comfort_value(value: Any) -> dict[str, Any]:
    if isinstance(value, dict):
        score = as_number_or_none(get_field(value, ["score", "value", "comfort", "comfort_score", "comfortScore"]))
        label = get_field(value, ["label", "text", "state", "comfort_label", "comfortLabel"])
        result: dict[str, Any] = {}
        if score is not None:
            result["comfort"] = score
        elif label is not None:
            result["comfort"] = str(label)
        if label is not None:
            result["comfortLabel"] = str(label)
        return result or {"comfort": json.dumps(value, ensure_ascii=False)}
    return {"comfort": value}
