import json
from pathlib import Path

from smart_space.core.settings import Settings
from smart_space.dashboard.state import DashboardState
from smart_space.mqtt.topics import build_mqtt_plan


class FakePublisher:
    def __init__(self):
        self.connected = {"light": True, "env": True, "wearable": True}
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


def test_shared_light_wearable_mqtt_client_subscribes_wearable_topics(tmp_path):
    settings = make_settings(tmp_path)

    plan = build_mqtt_plan(settings)

    assert "light" in plan
    assert "wearable" not in plan
    assert "integration/smart/v1/wearable/pico_wearable01/status" in plan["light"].subscribe_topics
    assert "integration/smart/v1/wearable/pico_wearable01/availability" in plan["light"].subscribe_topics


def test_env_status_with_occupancy_false_keeps_combined_env_module_online(tmp_path):
    state = DashboardState(make_settings(tmp_path), FakePublisher())

    state.handle_mqtt_message(
        "integration/smart/v1/env/rpi_env02/status",
        json.dumps({
            "type": "status",
            "node_id": "rpi_env02",
            "status": {
                "temperature_c": 30,
                "humidity_percent": 53,
                "air_quality_index": 190,
                "fan_level": 4,
                "duty": 44,
                "occupancy": False,
            },
        }),
    )

    env_module = next(item for item in state.module_snapshot() if item["key"] == "envComfort")
    assert env_module["status"] == "online"


def test_comfort_object_payload_is_normalized_for_dashboard_text(tmp_path):
    state = DashboardState(make_settings(tmp_path), FakePublisher())

    payload = state.handle_mqtt_message(
        "integration/smart/v1/env/rpi_env01/status",
        json.dumps({
            "type": "status",
            "node_id": "rpi_env01",
            "status": {
                "comfort": {
                    "score": 82.4,
                    "label": "舒適",
                },
            },
        }),
    )

    assert payload["sensorData"]["comfort"] == 82.4
    assert payload["sensorData"]["comfortLabel"] == "舒適"


def test_fan_command_updates_dashboard_state_optimistically(tmp_path):
    publisher = FakePublisher()
    state = DashboardState(make_settings(tmp_path), publisher)

    state.handle_dashboard_command({"action": "set_fan_level", "fanLevel": 5})

    assert state.last_payload["deviceState"]["fanSpeed"] == 5
    assert state.last_payload["deviceState"]["fanOn"] is True
    command = publisher.published[-1]
    assert command["name"] == "env"
    assert command["topic"] == "integration/smart/v1/env/rpi_env02/command"


def test_audio_module_stays_online_while_local_music_service_is_connected(tmp_path):
    state = DashboardState(make_settings(tmp_path), FakePublisher())

    state.broadcast_patch({"audioState": {"connected": True, "playing": True, "track": "focus.mp3"}})
    state.module_last_seen["audio"]["at"] = 0.0

    audio_module = next(item for item in state.module_snapshot() if item["key"] == "audio")

    assert audio_module["online"] is True
    assert audio_module["status"] == "online"


def test_camera_module_is_present_in_dashboard_module_snapshot(tmp_path):
    state = DashboardState(make_settings(tmp_path), FakePublisher())

    camera_module = next(item for item in state.module_snapshot() if item["key"] == "camera")

    assert camera_module["label"] == "空間影像串流"
    assert state.last_payload["connectionState"]["totalDevices"] == 5


def test_camera_status_patch_is_bridged_into_dashboard_payload(tmp_path):
    state = DashboardState(make_settings(tmp_path), FakePublisher())

    payload = state.broadcast_patch({
        "sensorData": {
            "cameraSnapshotUrl": "/api/camera/snapshot.jpg",
            "cameraHlsUrl": "/api/camera/hls/space/index.m3u8",
            "cameraWebrtcUrl": "http://127.0.0.1:8889/space",
            "cameraWhepUrl": "http://127.0.0.1:8889/space/whep",
        },
        "connectionState": {
            "moduleStatus": [
                {"key": "camera", "label": "空間影像串流", "online": True, "status": "online", "detail": "camera service"},
            ],
        },
    })

    assert payload["sensorData"]["cameraSnapshotUrl"] == "/api/camera/snapshot.jpg"
    assert payload["sensorData"]["cameraHlsUrl"] == "/api/camera/hls/space/index.m3u8"
    assert payload["sensorData"]["cameraWebrtcUrl"] == "http://127.0.0.1:8889/space"
    assert payload["sensorData"]["cameraWhepUrl"] == "http://127.0.0.1:8889/space/whep"
    assert any(item["key"] == "camera" for item in payload["connectionState"]["moduleStatus"])


def test_light_status_without_fan_fields_does_not_overwrite_env_fan_state(tmp_path):
    state = DashboardState(make_settings(tmp_path), FakePublisher())

    state.handle_mqtt_message(
        "integration/smart/v1/env/rpi_env02/status",
        json.dumps({
            "type": "status",
            "node_id": "rpi_env02",
            "status": {
                "fan_level": 5,
                "duty": 52,
                "mode": "MANUAL",
            },
        }),
    )

    state.handle_mqtt_message(
        "smartlight/bedroom01/status",
        json.dumps({
            "status": {
                "led_on": True,
                "brightness_pct": 64,
                "color_temp_k": 3900,
                "active_scene": "none",
            },
        }),
    )

    assert state.last_payload["deviceState"]["fanSpeed"] == 5
    assert state.last_payload["deviceState"]["fanOn"] is True


def test_light_status_exposes_current_lux_to_sensor_data_for_frontend(tmp_path):
    state = DashboardState(make_settings(tmp_path), FakePublisher())

    payload = state.handle_mqtt_message(
        "smartlight/bedroom01/status",
        json.dumps({
            "status": {
                "current_lux": 233.6,
                "target_lux": 500,
                "tolerance_lux": 75,
                "brightness_pct": 42,
                "led_on": True,
            },
        }),
    )

    assert payload["sensorData"]["currentLux"] == 233.6
    assert payload["sensorData"]["targetLux"] == 500
    assert payload["sensorData"]["toleranceLux"] == 75
    assert payload["sensorData"]["ledOutputPercent"] == 42


def test_wearable_activity_state_does_not_control_led_by_default(tmp_path):
    publisher = FakePublisher()
    state = DashboardState(make_settings(tmp_path), publisher)

    state.handle_mqtt_message(
        "integration/smart/v1/wearable/pico_wearable01/status",
        json.dumps({
            "type": "status",
            "node_id": "pico_wearable01",
            "status": {
                "heart_rate": 92,
                "spo2": 86,
                "summary": {"state": 1},
            },
        }),
    )

    assert not any(item["payload"].get("req_id", "").startswith("auto_activity_") for item in publisher.published)


def test_target_lux_rgb_custom_preview_uses_fixed_brightness_rgb_custom_command(tmp_path):
    publisher = FakePublisher()
    state = DashboardState(make_settings(tmp_path), publisher)

    state.handle_dashboard_command({
        "action": "set_custom_light",
        "custom_control_type": "target_lux_range",
        "color_mode": "rgb",
        "rgb": {"r": 12, "g": 34, "b": 56},
        "target_lux": 500,
        "tolerance_lux": 75,
        "min_output": 0,
        "max_output": 100,
    })

    command = next(item for item in publisher.published if item["payload"]["req_id"].startswith("web_custom_target_rgb"))
    assert command["topic"] == "smartlight/bedroom01/cmd"
    assert command["payload"]["cmd"] == "set_custom"
    assert command["payload"]["params"] == {
        "custom_control_type": "fixed_brightness",
        "fixed_brightness_pct": 64,
        "target_lux": None,
        "tolerance_lux": None,
        "target_lux_min": None,
        "target_lux_max": None,
        "min_output": 0,
        "max_output": 100,
        "color_mode": "rgb",
        "rgb": {"r": 12, "g": 34, "b": 56},
        "effect": "static",
    }
