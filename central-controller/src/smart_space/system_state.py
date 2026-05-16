from __future__ import annotations

import copy
import json
import threading
import time
from collections.abc import Callable
from typing import Any

from smart_space.core.payload_utils import (
    as_number_or_none,
    get_field,
    get_field_including_null,
    normalize_online_from_payload,
    normalize_timestamp_fields,
    payload_body,
    truthy_state,
)
from smart_space.core.settings import Settings
from smart_space.mqtt.topics import Topics


SUBSYSTEM_DEFAULTS = {
    "led": {"label": "LED系統"},
    "wearable": {"label": "穿戴式手環"},
    "envComfort": {"label": "環境舒適度/節能控制節點"},
    "audio": {"label": "音樂播放服務"},
    "camera": {"label": "空間影像串流"},
}

SMART_SCENE_TO_CONTEXT = {
    "vacant": "vacant",
    "sleep": "sleep",
    "relax": "relax",
    "work": "work",
    "exercise": "exercise",
}


class SystemStateManager:
    def __init__(self, settings: Settings):
        self.settings = settings
        self.topics = Topics(settings)
        self._lock = threading.RLock()
        self._context_listeners: list[Callable[[dict[str, Any]], None]] = []
        now_ms = _now_ms()
        self._state: dict[str, Any] = {
            "updatedAtMs": now_ms,
            "operationMode": {
                "smartModeEnabled": settings.smart_mode_enabled,
                "controlMode": "scene",
            },
            "currentContext": {
                "scene": "vacant",
                "source": "default",
                "confidence": None,
                "reason": "",
            },
            "subsystems": {
                key: {
                    "label": value["label"],
                    "online": None,
                    "status": "unknown",
                    "lastSeenAt": None,
                    "detail": "",
                }
                for key, value in SUBSYSTEM_DEFAULTS.items()
            },
            "sensors": {
                "heartRate": None,
                "spo2": None,
                "aqi": None,
                "temperature": None,
                "humidity": None,
                "comfort": None,
                "comfortLabel": None,
                "pirDetected": None,
                "pirTime": None,
                "currentLux": None,
                "targetLux": None,
                "toleranceLux": None,
                "cameraSnapshotUrl": None,
                "cameraHlsUrl": None,
                "cameraWebrtcUrl": None,
                "cameraWhepUrl": None,
            },
            "devices": {
                "light": {
                    "on": None,
                    "brightness": None,
                    "kelvin": None,
                    "activeScene": None,
                    "currentLux": None,
                    "targetLux": None,
                    "toleranceLux": None,
                },
                "fan": {
                    "on": None,
                    "speed": None,
                    "mode": None,
                },
                "audio": {
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
                "camera": {
                    "connected": None,
                    "streamPath": None,
                    "rtspUrl": None,
                    "webrtcUrl": None,
                    "whepUrl": None,
                    "hlsUrl": None,
                    "snapshotUrl": None,
                    "lastFrameAtMs": None,
                    "lastError": None,
                },
            },
            "connections": {
                "light": False,
                "env": False,
                "wearable": False,
            },
        }

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            return copy.deepcopy(self._state)

    def add_context_listener(self, listener: Callable[[dict[str, Any]], None]) -> None:
        self._context_listeners.append(listener)

    def remove_context_listener(self, listener: Callable[[dict[str, Any]], None]) -> None:
        if listener in self._context_listeners:
            self._context_listeners.remove(listener)

    def update_audio_state(self, audio_patch: dict[str, Any]) -> dict[str, Any]:
        allowed_keys = {
            "connected",
            "playing",
            "state",
            "mode",
            "scene",
            "autoModeEnabled",
            "volume",
            "track",
            "trackPath",
            "outputDevice",
            "progressSec",
            "durationSec",
        }
        with self._lock:
            for key in allowed_keys:
                if key in audio_patch:
                    self._state["devices"]["audio"][key] = audio_patch[key]
            self._mark_subsystem("audio", audio_patch.get("connected") is not False, "local music service")
            self._touch()
            return copy.deepcopy(self._state)

    def update_camera_state(self, camera_patch: dict[str, Any]) -> dict[str, Any]:
        allowed_keys = {
            "connected",
            "streamPath",
            "rtspUrl",
            "webrtcUrl",
            "whepUrl",
            "hlsUrl",
            "snapshotUrl",
            "lastFrameAtMs",
            "lastError",
        }
        with self._lock:
            for key in allowed_keys:
                if key in camera_patch:
                    self._state["devices"]["camera"][key] = camera_patch[key]
            if "snapshotUrl" in camera_patch:
                self._state["sensors"]["cameraSnapshotUrl"] = camera_patch["snapshotUrl"]
            if "hlsUrl" in camera_patch:
                self._state["sensors"]["cameraHlsUrl"] = camera_patch["hlsUrl"]
            if "webrtcUrl" in camera_patch:
                self._state["sensors"]["cameraWebrtcUrl"] = camera_patch["webrtcUrl"]
            if "whepUrl" in camera_patch:
                self._state["sensors"]["cameraWhepUrl"] = camera_patch["whepUrl"]
            self._mark_subsystem("camera", camera_patch.get("connected") is True, "space camera service")
            self._touch()
            return copy.deepcopy(self._state)

    def set_mqtt_connection(self, name: str, connected: bool) -> dict[str, Any]:
        with self._lock:
            if name in self._state["connections"]:
                self._state["connections"][name] = bool(connected)
            self._touch()
            return copy.deepcopy(self._state)

    def activate_system(self, *, source: str, reason: str = "") -> dict[str, Any]:
        notify_snapshot: dict[str, Any] | None = None
        with self._lock:
            old_scene = self._state["currentContext"]["scene"]
            self._activate_system_locked(source=source, reason=reason)
            self._touch()
            snapshot = copy.deepcopy(self._state)
            if self._state["currentContext"]["scene"] != old_scene:
                notify_snapshot = snapshot
        if notify_snapshot:
            self._notify_context_listeners(notify_snapshot)
        return snapshot

    def set_operation_mode(
        self,
        *,
        smart_mode_enabled: bool | None = None,
        control_mode: str | None = None,
        scene: str | None = None,
        source: str = "system",
        confidence: float | None = None,
        reason: str = "",
    ) -> dict[str, Any]:
        notify_snapshot: dict[str, Any] | None = None
        with self._lock:
            old_scene = self._state["currentContext"]["scene"]
            if smart_mode_enabled is not None:
                self._state["operationMode"]["smartModeEnabled"] = bool(smart_mode_enabled)
            if control_mode:
                self._state["operationMode"]["controlMode"] = control_mode
            if scene:
                self._state["currentContext"].update({
                    "scene": scene,
                    "source": source,
                    "confidence": confidence,
                    "reason": reason,
                })
            self._touch()
            snapshot = copy.deepcopy(self._state)
            if self._state["currentContext"]["scene"] != old_scene:
                notify_snapshot = snapshot
        if notify_snapshot:
            self._notify_context_listeners(notify_snapshot)
        return snapshot

    def apply_mqtt_message(self, topic: str, raw: str | bytes) -> dict[str, Any]:
        raw_text = raw.decode() if isinstance(raw, bytes) else str(raw)
        try:
            data = json.loads(raw_text)
        except json.JSONDecodeError:
            data = {"value": raw_text}

        notify_snapshot: dict[str, Any] | None = None
        with self._lock:
            old_scene = self._state["currentContext"]["scene"]
            parts = self.topics.integration_parts(topic)
            if topic.endswith("/availability"):
                self._apply_availability(topic, raw_text, data, parts)
            elif topic.endswith("/status") or topic.endswith("/event"):
                body = normalize_timestamp_fields(payload_body(data))
                if self._is_light_status(topic, parts):
                    self._apply_light_status(body)
                self._apply_generic_status(topic, body, parts)
            self._touch()
            snapshot = copy.deepcopy(self._state)
            if self._state["currentContext"]["scene"] != old_scene:
                notify_snapshot = snapshot
        if notify_snapshot:
            self._notify_context_listeners(notify_snapshot)
        return snapshot

    def _apply_availability(self, topic: str, raw: str, data: dict[str, Any], parts: dict[str, str] | None) -> None:
        online = normalize_online_from_payload(raw, data)
        if parts:
            subsystem = self._subsystem_for_domain(parts["domain"])
            if subsystem:
                self._mark_subsystem(subsystem, online, f"{parts['domain']}/{parts['node_id']} availability")
            return
        if topic.startswith(f"{self.topics.base_topic}/"):
            self._mark_subsystem("led", online, f"SmartLight NODE {self.settings.smartlight_node_id} availability")

    def _apply_light_status(self, body: dict[str, Any]) -> None:
        active_scene = body.get("active_scene") or "none"
        brightness = as_number_or_none(get_field(body, ["brightness_pct", "brightness", "output_percent", "led_output_percent"]))
        kelvin = as_number_or_none(get_field(body, ["color_temp_k", "kelvin"]))
        current_lux = as_number_or_none(get_field(body, ["current_lux", "ambient_lux", "lux"]))
        target_lux = as_number_or_none(get_field_including_null(body, ["target_lux"]))
        tolerance_lux = as_number_or_none(get_field_including_null(body, ["tolerance_lux"]))
        led_on_raw = truthy_state(get_field(body, ["led_on", "ledOn", "power", "enabled"]))
        flow_enabled = body.get("flow_enabled") is True
        led_on = led_on_raw if led_on_raw is not None else (brightness is not None and (brightness > 0 or flow_enabled))

        self._state["devices"]["light"].update({
            "on": led_on,
            "brightness": brightness,
            "kelvin": kelvin,
            "activeScene": active_scene,
            "currentLux": current_lux,
            "targetLux": target_lux,
            "toleranceLux": tolerance_lux,
        })
        self._state["sensors"].update({
            "currentLux": current_lux,
            "targetLux": target_lux,
            "toleranceLux": tolerance_lux,
        })
        self._mark_subsystem("led", True, f"SmartLight NODE {self.settings.smartlight_node_id} status")

        context_scene = SMART_SCENE_TO_CONTEXT.get(active_scene)
        if context_scene:
            self._state["currentContext"].update({
                "scene": context_scene,
                "source": "light_status",
                "confidence": None,
                "reason": "",
            })

    def _apply_generic_status(self, topic: str, body: dict[str, Any], parts: dict[str, str] | None) -> None:
        subsystem = self._subsystem_for_domain(parts["domain"]) if parts else None

        heart = as_number_or_none(get_field(body, ["heartRate", "heart_rate", "bpm", "hr", "heart"]))
        if heart is not None:
            self._state["sensors"]["heartRate"] = heart
            self._mark_subsystem("wearable", True, "wearable status")

        spo2 = as_number_or_none(get_field(body, ["spo2", "SpO2", "blood_oxygen", "bloodOxygen", "oxygen"]))
        if spo2 is not None:
            self._state["sensors"]["spo2"] = spo2
            self._mark_subsystem("wearable", True, "wearable status")

        aqi = as_number_or_none(get_field_including_null(body, ["aqi", "AQI", "air_quality", "airQuality", "air_quality_index", "air_raw", "pm25", "pm2_5"]))
        if aqi is not None:
            self._state["sensors"]["aqi"] = aqi
            self._mark_subsystem("envComfort", True, "env status")

        temperature = as_number_or_none(get_field_including_null(body, ["temperature", "temp", "temperature_c", "temperatureC"]))
        if temperature is not None:
            self._state["sensors"]["temperature"] = temperature
            self._mark_subsystem("envComfort", True, "env status")

        humidity = as_number_or_none(get_field_including_null(body, ["humidity", "humid", "humidity_percent", "rh"]))
        if humidity is not None:
            self._state["sensors"]["humidity"] = humidity
            self._mark_subsystem("envComfort", True, "env status")

        pir_raw = get_field_including_null(body, ["pir", "motion", "presence", "occupancy", "human", "humanDetected", "detected", "value"])
        pir = None if pir_raw is None else truthy_state(pir_raw)
        if pir is not None:
            self._state["sensors"]["pirDetected"] = pir
            self._state["sensors"]["pirTime"] = time.strftime("%H:%M:%S")
            self._mark_subsystem("envComfort", True, "env status")
            if pir:
                self._activate_system_locked(source="pir", reason="PIR detected occupancy")

        comfort = get_field(body, ["comfort", "comfort_score", "comfortScore", "comfort_label", "comfortLabel"])
        if comfort is not None:
            self._apply_comfort(comfort)
            self._mark_subsystem("envComfort", True, "env status")

        fan_speed = as_number_or_none(get_field(body, ["fanSpeed", "fan_speed", "fanLevel", "fan_level", "speed"]))
        fan_on = truthy_state(get_field(body, ["fanOn", "fan_on", "fanEnabled", "fan_enabled", "enabled"]))
        fan_mode = get_field(body, ["mode", "fanMode", "fan_mode"])
        if fan_speed is not None:
            self._state["devices"]["fan"]["speed"] = fan_speed
            self._state["devices"]["fan"]["on"] = fan_on if fan_on is not None else fan_speed > 0
            self._mark_subsystem("envComfort", True, "fan status")
        elif fan_on is not None:
            self._state["devices"]["fan"]["on"] = fan_on
            self._mark_subsystem("envComfort", True, "fan status")
        if fan_mode is not None:
            self._state["devices"]["fan"]["mode"] = str(fan_mode)

        if parts and parts["domain"] == "audio":
            self._apply_audio_status(body)
        if parts and parts["domain"] in {"camera", "vision"}:
            self._apply_camera_status(body)
        if subsystem:
            self._mark_subsystem(subsystem, True, f"{parts['domain']} status")

    def _apply_audio_status(self, body: dict[str, Any]) -> None:
        audio_connected = truthy_state(get_field(body, ["audioConnected", "bluetoothConnected", "connected", "online"]))
        if audio_connected is not None:
            self._state["devices"]["audio"]["connected"] = audio_connected
        for output_key, names in {
            "playing": ["playing", "isPlaying"],
            "volume": ["volume", "volume_pct", "volumePct"],
            "track": ["track", "title", "song"],
            "mode": ["mode", "audioMode"],
            "progressSec": ["progressSec", "progress_sec", "positionSec", "position"],
            "durationSec": ["durationSec", "duration_sec", "duration"],
        }.items():
            value = get_field(body, names)
            if value is None:
                continue
            self._state["devices"]["audio"][output_key] = (
                truthy_state(value) if output_key == "playing"
                else as_number_or_none(value) if output_key in {"volume", "progressSec", "durationSec"}
                else value
            )

    def _apply_camera_status(self, body: dict[str, Any]) -> None:
        snapshot_url = get_field(body, ["snapshotUrl", "snapshot_url", "imageUrl", "image_url"])
        hls_url = get_field(body, ["hlsUrl", "hls_url"])
        webrtc_url = get_field(body, ["webrtcUrl", "webrtc_url", "webRtcUrl", "web_rtc_url"])
        whep_url = get_field(body, ["whepUrl", "whep_url"])
        stream_path = get_field(body, ["streamPath", "stream_path"])
        rtsp_url = get_field(body, ["rtspUrl", "rtsp_url", "streamUrl", "stream_url"])
        connected = truthy_state(get_field(body, ["cameraOnline", "camera_connected", "cameraConnected", "online", "connected"]))
        if connected is not None:
            self._state["devices"]["camera"]["connected"] = connected
        if stream_path:
            self._state["devices"]["camera"]["streamPath"] = stream_path
        if rtsp_url:
            self._state["devices"]["camera"]["rtspUrl"] = rtsp_url
        if hls_url:
            self._state["devices"]["camera"]["hlsUrl"] = hls_url
            self._state["sensors"]["cameraHlsUrl"] = hls_url
        if webrtc_url:
            self._state["devices"]["camera"]["webrtcUrl"] = webrtc_url
            self._state["sensors"]["cameraWebrtcUrl"] = webrtc_url
        if whep_url:
            self._state["devices"]["camera"]["whepUrl"] = whep_url
            self._state["sensors"]["cameraWhepUrl"] = whep_url
        if snapshot_url:
            self._state["sensors"]["cameraSnapshotUrl"] = snapshot_url
            self._state["devices"]["camera"]["snapshotUrl"] = snapshot_url
        last_frame_at_ms = as_number_or_none(get_field(body, ["lastFrameAtMs", "last_frame_at_ms"]))
        if last_frame_at_ms is not None:
            self._state["devices"]["camera"]["lastFrameAtMs"] = last_frame_at_ms
        last_error = get_field(body, ["lastError", "last_error"])
        if last_error is not None:
            self._state["devices"]["camera"]["lastError"] = str(last_error)

    def _apply_comfort(self, value: Any) -> None:
        if isinstance(value, dict):
            score = as_number_or_none(get_field(value, ["score", "value", "comfort", "comfort_score", "comfortScore"]))
            label = get_field(value, ["label", "text", "state", "comfort_label", "comfortLabel"])
            if score is not None:
                self._state["sensors"]["comfort"] = score
            elif label is not None:
                self._state["sensors"]["comfort"] = str(label)
            if label is not None:
                self._state["sensors"]["comfortLabel"] = str(label)
            return
        self._state["sensors"]["comfort"] = value

    def _is_light_status(self, topic: str, parts: dict[str, str] | None) -> bool:
        return topic.startswith(f"{self.topics.base_topic}/") or bool(parts and parts["domain"] == "light")

    def _mark_subsystem(self, key: str, online: bool, detail: str) -> None:
        if key not in self._state["subsystems"]:
            return
        self._state["subsystems"][key].update({
            "online": bool(online),
            "status": "online" if online else "offline",
            "lastSeenAt": _now_ms() if online else None,
            "detail": detail,
        })

    @staticmethod
    def _subsystem_for_domain(domain: str) -> str | None:
        if domain == "wearable":
            return "wearable"
        if domain == "env":
            return "envComfort"
        if domain == "light":
            return "led"
        if domain == "audio":
            return "audio"
        if domain in {"camera", "vision"}:
            return "camera"
        return None

    def _touch(self) -> None:
        self._state["updatedAtMs"] = _now_ms()

    def _activate_system_locked(self, *, source: str, reason: str = "") -> None:
        if self._state["currentContext"]["scene"] != "vacant":
            return
        self._state["operationMode"].update({
            "smartModeEnabled": True,
            "controlMode": "scene",
        })
        self._state["currentContext"].update({
            "scene": "relax",
            "source": source,
            "confidence": None,
            "reason": reason,
        })

    def _notify_context_listeners(self, snapshot: dict[str, Any]) -> None:
        for listener in list(self._context_listeners):
            listener(copy.deepcopy(snapshot))


def _now_ms() -> int:
    return round(time.time() * 1000)
