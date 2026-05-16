import json
from pathlib import Path

from smart_space.core.settings import Settings
from smart_space.system_state import SystemStateManager


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


def test_initial_snapshot_exposes_neutral_system_state_shape(tmp_path):
    state = SystemStateManager(make_settings(tmp_path))

    snapshot = state.snapshot()

    assert set(snapshot) == {
        "updatedAtMs",
        "operationMode",
        "currentContext",
        "subsystems",
        "sensors",
        "devices",
        "connections",
    }
    assert snapshot["operationMode"] == {
        "smartModeEnabled": True,
        "controlMode": "scene",
    }
    assert snapshot["currentContext"] == {
        "scene": "vacant",
        "source": "default",
        "confidence": None,
        "reason": "",
    }
    assert snapshot["subsystems"]["led"]["status"] == "unknown"
    assert snapshot["subsystems"]["audio"]["status"] == "unknown"
    assert snapshot["subsystems"]["camera"]["status"] == "unknown"
    assert snapshot["sensors"]["heartRate"] is None
    assert snapshot["devices"]["light"]["brightness"] is None
    assert snapshot["devices"]["camera"] == {
        "connected": None,
        "streamPath": None,
        "rtspUrl": None,
        "webrtcUrl": None,
        "whepUrl": None,
        "hlsUrl": None,
        "snapshotUrl": None,
        "lastFrameAtMs": None,
        "lastError": None,
    }
    assert snapshot["connections"] == {"light": False, "env": False, "wearable": False}


def test_mqtt_status_updates_sensors_devices_and_subsystems(tmp_path):
    state = SystemStateManager(make_settings(tmp_path))

    state.apply_mqtt_message(
        "integration/smart/v1/wearable/pico_wearable01/status",
        json.dumps({"status": {"heart_rate": 91, "spo2": 97}}),
    )
    state.apply_mqtt_message(
        "integration/smart/v1/env/rpi_env02/status",
        json.dumps({"status": {"temperature_c": 29.5, "humidity_percent": 58, "air_quality_index": 180, "fan_level": 4}}),
    )
    snapshot = state.apply_mqtt_message(
        "smartlight/bedroom01/status",
        json.dumps({"status": {"led_on": True, "brightness_pct": 42, "color_temp_k": 3900, "active_scene": "work", "current_lux": 233.6}}),
    )

    assert snapshot["sensors"]["heartRate"] == 91
    assert snapshot["sensors"]["spo2"] == 97
    assert snapshot["sensors"]["temperature"] == 29.5
    assert snapshot["sensors"]["humidity"] == 58
    assert snapshot["sensors"]["aqi"] == 180
    assert snapshot["sensors"]["currentLux"] == 233.6
    assert snapshot["devices"]["light"] == {
        "on": True,
        "brightness": 42,
        "kelvin": 3900,
        "activeScene": "work",
        "currentLux": 233.6,
        "targetLux": None,
        "toleranceLux": None,
    }
    assert snapshot["devices"]["fan"]["speed"] == 4
    assert snapshot["devices"]["fan"]["on"] is True
    assert snapshot["subsystems"]["wearable"]["status"] == "online"
    assert snapshot["subsystems"]["envComfort"]["status"] == "online"
    assert snapshot["subsystems"]["led"]["status"] == "online"
    assert snapshot["currentContext"]["scene"] == "work"
    assert snapshot["currentContext"]["source"] == "light_status"


def test_availability_offline_updates_subsystem_status(tmp_path):
    state = SystemStateManager(make_settings(tmp_path))

    snapshot = state.apply_mqtt_message(
        "integration/smart/v1/env/rpi_env01/availability",
        json.dumps({"availability": {"online": False}}),
    )

    assert snapshot["subsystems"]["envComfort"]["status"] == "offline"
    assert snapshot["subsystems"]["envComfort"]["online"] is False


def test_set_operation_mode_updates_mode_and_context(tmp_path):
    state = SystemStateManager(make_settings(tmp_path))

    snapshot = state.set_operation_mode(smart_mode_enabled=False, control_mode="manual", scene="relax", source="dashboard")

    assert snapshot["operationMode"] == {
        "smartModeEnabled": False,
        "controlMode": "manual",
    }
    assert snapshot["currentContext"]["scene"] == "relax"
    assert snapshot["currentContext"]["source"] == "dashboard"


def test_activate_system_switches_vacant_space_to_relax_smart_scene(tmp_path):
    state = SystemStateManager(make_settings(tmp_path))
    seen = []
    state.add_context_listener(lambda snapshot: seen.append(snapshot["currentContext"]["scene"]))

    snapshot = state.activate_system(source="gpio_button", reason="button pressed")

    assert snapshot["operationMode"] == {
        "smartModeEnabled": True,
        "controlMode": "scene",
    }
    assert snapshot["currentContext"] == {
        "scene": "relax",
        "source": "gpio_button",
        "confidence": None,
        "reason": "button pressed",
    }
    assert seen == ["relax"]


def test_pir_true_activates_vacant_space_to_relax_scene(tmp_path):
    state = SystemStateManager(make_settings(tmp_path))

    snapshot = state.apply_mqtt_message(
        "integration/smart/v1/env/rpi_env01/status",
        json.dumps({"status": {"occupancy": True}}),
    )

    assert snapshot["sensors"]["pirDetected"] is True
    assert snapshot["operationMode"]["smartModeEnabled"] is True
    assert snapshot["currentContext"]["scene"] == "relax"
    assert snapshot["currentContext"]["source"] == "pir"


def test_update_audio_state_updates_device_and_subsystem(tmp_path):
    state = SystemStateManager(make_settings(tmp_path))

    snapshot = state.update_audio_state({
        "connected": True,
        "playing": True,
        "state": "PLAYING",
        "mode": "relax",
        "scene": "relax",
        "autoModeEnabled": True,
        "volume": 55,
        "track": "calm.mp3",
        "trackPath": "/tmp/calm.mp3",
        "outputDevice": "speaker",
    })

    assert snapshot["devices"]["audio"]["connected"] is True
    assert snapshot["devices"]["audio"]["playing"] is True
    assert snapshot["devices"]["audio"]["state"] == "PLAYING"
    assert snapshot["devices"]["audio"]["track"] == "calm.mp3"
    assert snapshot["subsystems"]["audio"]["status"] == "online"


def test_update_camera_state_updates_device_and_subsystem(tmp_path):
    state = SystemStateManager(make_settings(tmp_path))

    snapshot = state.update_camera_state({
        "connected": True,
        "streamPath": "space",
        "rtspUrl": "rtsp://127.0.0.1:8554/space",
        "webrtcUrl": "http://127.0.0.1:8889/space",
        "whepUrl": "http://127.0.0.1:8889/space/whep",
        "hlsUrl": "/api/camera/hls/space/index.m3u8",
        "snapshotUrl": "/api/camera/snapshot.jpg",
        "lastFrameAtMs": 1710000000000,
        "lastError": None,
    })

    assert snapshot["devices"]["camera"] == {
        "connected": True,
        "streamPath": "space",
        "rtspUrl": "rtsp://127.0.0.1:8554/space",
        "webrtcUrl": "http://127.0.0.1:8889/space",
        "whepUrl": "http://127.0.0.1:8889/space/whep",
        "hlsUrl": "/api/camera/hls/space/index.m3u8",
        "snapshotUrl": "/api/camera/snapshot.jpg",
        "lastFrameAtMs": 1710000000000,
        "lastError": None,
    }
    assert snapshot["subsystems"]["camera"]["status"] == "online"


def test_context_listener_fires_only_when_scene_changes(tmp_path):
    state = SystemStateManager(make_settings(tmp_path))
    seen = []
    state.add_context_listener(lambda snapshot: seen.append(snapshot["currentContext"]["scene"]))

    state.set_operation_mode(control_mode="scene")
    state.set_operation_mode(scene="work", source="dashboard")
    state.set_operation_mode(scene="relax", source="dashboard")

    assert seen == ["work", "relax"]


def test_system_state_module_does_not_depend_on_dashboard_package():
    source = Path("src/smart_space/system_state.py").read_text(encoding="utf-8")

    assert "smart_space.dashboard" not in source
