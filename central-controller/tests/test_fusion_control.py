import asyncio
import json
from pathlib import Path

import pytest

from smart_space.core.settings import Settings
from smart_space.services.fusion_control import FusionControlService, fan_level_for_scene
from smart_space.system_state import SystemStateManager


class FakePublisher:
    def __init__(self):
        self.connected = {"light": True, "env": True}
        self.published = []

    def is_connected(self, name):
        return self.connected.get(name, False)

    def publish(self, name, topic, payload, qos=1):
        self.published.append({
            "name": name,
            "topic": topic,
            "payload": json.loads(payload),
            "qos": qos,
        })


class FakeMqttMessage:
    def __init__(self, topic, payload):
        self.topic = topic
        self.payload = payload


class FakeMqttClient:
    def __init__(self):
        self.on_connect = None
        self.on_message = None
        self.subscriptions = []
        self.connected = False

    def username_pw_set(self, username, password=None):
        return None

    def connect(self, host, port, keepalive=30):
        self.connected = True
        self.host = host
        self.port = port
        self.keepalive = keepalive
        if self.on_connect:
            self.on_connect(self, None, None, 0, None)
        return 0

    def loop_start(self):
        return None

    def subscribe(self, topic, qos=0):
        self.subscriptions.append((topic, qos))

    def emit(self, topic, payload):
        self.on_message(self, None, FakeMqttMessage(topic, payload))

    def loop_stop(self):
        return None

    def disconnect(self):
        self.connected = False


def make_settings(tmp_path):
    return Settings(
        port=0,
        mqtt_host="localhost",
        wearable_mqtt_host="localhost",
        env_mqtt_host="env-broker.local",
        dashboard_dir=Path("src/smart_space/web/dashboard"),
        led_profile_default_path=Path("src/smart_space/config/led_profiles.default.json"),
        led_profile_user_path=tmp_path / "led_profiles.user.json",
    )


@pytest.mark.asyncio
async def test_fusion_ignores_observation_when_smart_mode_disabled(tmp_path):
    settings = make_settings(tmp_path)
    state = SystemStateManager(settings)
    state.set_operation_mode(smart_mode_enabled=False, control_mode="manual", scene="relax", source="dashboard")
    publisher = FakePublisher()
    service = FusionControlService(settings, state, publisher)

    result = await service.apply_observation({"scene_state": "WORKING", "mapped_scene": "work", "confidence": 0.9, "reason": "desk"})

    assert result["applied"] is False
    assert result["reason"] == "smart_mode_disabled"
    assert state.snapshot()["currentContext"]["scene"] == "relax"
    assert publisher.published == []


@pytest.mark.asyncio
async def test_fusion_keeps_scene_for_active_and_unknown_results(tmp_path):
    settings = make_settings(tmp_path)
    state = SystemStateManager(settings)
    state.activate_system(source="gpio_button")
    publisher = FakePublisher()
    service = FusionControlService(settings, state, publisher)

    active = await service.apply_observation({"scene_state": "ACTIVE", "mapped_scene": None, "confidence": 0.5, "reason": "moving"})
    unknown = await service.apply_observation({"scene_state": "UNKNOWN", "mapped_scene": None, "confidence": 0.2, "reason": "unclear"})

    assert active["applied"] is False
    assert unknown["applied"] is False
    assert state.snapshot()["currentContext"]["scene"] == "relax"
    assert publisher.published == []


@pytest.mark.asyncio
async def test_fusion_requires_two_consecutive_empty_results_before_vacant(tmp_path):
    settings = make_settings(tmp_path)
    state = SystemStateManager(settings)
    state.activate_system(source="pir")
    publisher = FakePublisher()
    service = FusionControlService(settings, state, publisher)

    first = await service.apply_observation({"scene_state": "EMPTY", "mapped_scene": "vacant", "confidence": 0.8, "reason": "empty"})
    second = await service.apply_observation({"scene_state": "EMPTY", "mapped_scene": "vacant", "confidence": 0.85, "reason": "still empty"})

    assert first["applied"] is False
    assert first["reason"] == "waiting_for_consecutive_vacant"
    assert second["applied"] is True
    assert state.snapshot()["currentContext"]["scene"] == "vacant"
    assert [item["payload"].get("cmd") for item in publisher.published if item["name"] == "light"] == ["set_scene"]
    assert publisher.published[1]["payload"]["command"] == "set_mode"
    assert publisher.published[1]["payload"]["params"] == {"mode": "OFF"}
    assert publisher.published[2]["payload"]["command"] == "set_fan_level"
    assert publisher.published[2]["payload"]["params"] == {"fan_level": 0}


@pytest.mark.asyncio
async def test_fusion_does_not_republish_vacant_after_entering_vacant(tmp_path):
    settings = make_settings(tmp_path)
    state = SystemStateManager(settings)
    state.activate_system(source="pir")
    publisher = FakePublisher()
    service = FusionControlService(settings, state, publisher)

    await service.apply_observation({"scene_state": "EMPTY", "confidence": 0.8})
    await service.apply_observation({"scene_state": "EMPTY", "confidence": 0.8})
    published_after_vacant = len(publisher.published)
    result = await service.apply_observation({"scene_state": "EMPTY", "confidence": 0.8})

    assert result["applied"] is False
    assert result["reason"] == "already_vacant"
    assert state.snapshot()["currentContext"]["scene"] == "vacant"
    assert len(publisher.published) == published_after_vacant


@pytest.mark.asyncio
async def test_fusion_button_activation_ignores_stale_empty_from_previous_vacant_cycle(tmp_path):
    settings = make_settings(tmp_path)
    state = SystemStateManager(settings)
    publisher = FakePublisher()
    service = FusionControlService(settings, state, publisher)

    service.handle_gpio_button_pressed({"eventType": "gpio_button_pressed"})
    activated_at = service.status()["lastActivationAtMs"]
    stale_observation = {
        "scene_state": "EMPTY",
        "confidence": 0.8,
        "request_started_at_ms": activated_at - 1,
        "request_context_scene": "vacant",
    }
    first = await service.apply_observation(stale_observation)
    second = await service.apply_observation(stale_observation)

    assert first["applied"] is False
    assert first["reason"] == "stale_observation"
    assert second["applied"] is False
    assert state.snapshot()["currentContext"]["scene"] == "relax"
    assert [item["payload"].get("cmd") for item in publisher.published if item["name"] == "light"] == ["set_scene"]


@pytest.mark.asyncio
async def test_fusion_resets_empty_count_when_occupied_scene_arrives(tmp_path):
    settings = make_settings(tmp_path)
    state = SystemStateManager(settings)
    state.activate_system(source="pir")
    publisher = FakePublisher()
    service = FusionControlService(settings, state, publisher)

    await service.apply_observation({"scene_state": "EMPTY", "mapped_scene": "vacant", "confidence": 0.8})
    await service.apply_observation({"scene_state": "WORKING", "mapped_scene": "work", "confidence": 0.9})
    result = await service.apply_observation({"scene_state": "EMPTY", "mapped_scene": "vacant", "confidence": 0.8})

    assert result["applied"] is False
    assert state.snapshot()["currentContext"]["scene"] == "work"


@pytest.mark.parametrize(
    ("scene", "sensors", "expected"),
    [
        ("vacant", {}, 0),
        ("sleep", {}, 3),
        ("work", {}, 5),
        ("relax", {}, 5),
        ("exercise", {}, 8),
        ("relax", {"temperature": 29, "humidity": 72, "aqi": 160, "heartRate": 105}, 9),
        ("exercise", {"temperature": 32, "humidity": 72, "aqi": 230, "heartRate": 130}, 10),
    ],
)
def test_fan_level_for_scene_uses_base_speed_sensor_boosts_and_clamp(scene, sensors, expected):
    assert fan_level_for_scene(scene, sensors) == expected


@pytest.mark.asyncio
async def test_fusion_publishes_light_and_fan_commands_for_valid_scene(tmp_path):
    settings = make_settings(tmp_path)
    state = SystemStateManager(settings)
    state.activate_system(source="gpio_button")
    state.apply_mqtt_message(
        "integration/smart/v1/env/rpi_env01/status",
        json.dumps({"status": {"temperature_c": 29, "humidity_percent": 72, "air_quality_index": 160}}),
    )
    state.apply_mqtt_message("integration/smart/v1/wearable/pico_wearable01/status", json.dumps({"status": {"heart_rate": 105}}))
    publisher = FakePublisher()
    service = FusionControlService(settings, state, publisher)

    result = await service.apply_observation({"scene_state": "RELAXING", "mapped_scene": "relax", "confidence": 0.87, "reason": "reading"})

    assert result["applied"] is True
    assert state.snapshot()["currentContext"]["source"] == "fusion_control"
    light_command = publisher.published[0]
    fan_mode_command = publisher.published[1]
    fan_level_command = publisher.published[2]
    assert light_command["topic"] == "smartlight/bedroom01/cmd"
    assert light_command["payload"]["cmd"] == "set_scene"
    assert light_command["payload"]["params"] == {"scene": "relax"}
    assert fan_mode_command["payload"]["command"] == "set_mode"
    assert fan_mode_command["payload"]["params"] == {"mode": "MANUAL"}
    assert fan_level_command["topic"] == "integration/smart/v1/env/rpi_env02/command"
    assert fan_level_command["payload"]["command"] == "set_fan_level"
    assert fan_level_command["payload"]["params"] == {"fan_level": 9}


@pytest.mark.asyncio
async def test_fusion_can_apply_relax_controls_from_gpio_activation_without_ai_observation(tmp_path):
    settings = make_settings(tmp_path)
    state = SystemStateManager(settings)
    publisher = FakePublisher()
    service = FusionControlService(settings, state, publisher)

    result = await service.apply_scene("relax", source="gpio_button", reason="GPIO button pressed")

    assert result["applied"] is True
    assert state.snapshot()["currentContext"]["scene"] == "relax"
    assert state.snapshot()["currentContext"]["source"] == "gpio_button"
    assert publisher.published[0]["payload"]["cmd"] == "set_scene"
    assert publisher.published[0]["payload"]["params"] == {"scene": "relax"}
    assert publisher.published[2]["payload"]["command"] == "set_fan_level"
    assert publisher.published[2]["payload"]["params"] == {"fan_level": 5}


@pytest.mark.asyncio
async def test_fusion_subscribes_button_event_and_applies_relax(tmp_path):
    settings = make_settings(tmp_path)
    state = SystemStateManager(settings)
    publisher = FakePublisher()
    mqtt = FakeMqttClient()
    service = FusionControlService(settings, state, publisher, mqtt_client_factory=lambda: mqtt, gpio_button_service=lambda: None)

    service.start(loop=asyncio.get_running_loop())
    mqtt.emit(
        "integration/smart/v1/controller/smart_space_master/event",
        json.dumps({"event": {"eventType": "gpio_button_pressed", "pin": 17, "targetScene": "relax"}}).encode(),
    )
    await asyncio.sleep(0)

    assert mqtt.subscriptions == [("integration/smart/v1/controller/smart_space_master/event", 1)]
    assert state.snapshot()["currentContext"]["scene"] == "relax"
    assert state.snapshot()["currentContext"]["source"] == "gpio_button"
    assert publisher.published[0]["payload"]["cmd"] == "set_scene"
    assert publisher.published[0]["payload"]["params"] == {"scene": "relax"}
