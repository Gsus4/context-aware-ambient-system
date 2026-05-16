from pathlib import Path

from fastapi.testclient import TestClient

import smart_space.api.app as app_module
from smart_space.api.app import create_app
from smart_space.core.settings import Settings
from smart_space.services.interfaces import CameraFrame


class FakePublisher:
    def __init__(self):
        self.connected = {"light": True, "env": True, "wearable": True}
        self.published = []

    def is_connected(self, name):
        return self.connected.get(name, False)

    def publish(self, name, topic, payload, qos=1):
        self.published.append((name, topic, payload, qos))


class FakeCameraService:
    def __init__(self, frame: CameraFrame | None = None):
        self._frame = frame
        self._status = {
            "connected": frame is not None,
            "streamPath": "space",
            "rtspUrl": "rtsp://127.0.0.1:8554/space",
            "webrtcUrl": "http://127.0.0.1:8889/space",
            "whepUrl": "http://127.0.0.1:8889/space/whep",
            "hlsUrl": "/api/camera/hls/space/index.m3u8",
            "snapshotUrl": "/api/camera/snapshot.jpg",
            "lastFrameAtMs": frame.captured_at_ms if frame else None,
            "lastError": None,
        }

    def status(self):
        return dict(self._status)

    async def latest_frame(self):
        return self._frame


def make_client(tmp_path, camera_service=None):
    settings = Settings(
        port=0,
        mqtt_host="localhost",
        wearable_mqtt_host="localhost",
        env_mqtt_host="env-broker.local",
        dashboard_dir=Path("src/smart_space/web/dashboard"),
        led_profile_default_path=Path("src/smart_space/config/led_profiles.default.json"),
        led_profile_user_path=tmp_path / "led_profiles.user.json",
        camera_enabled=False,
        gpio_button_enabled=False,
    )
    publisher = FakePublisher()
    return TestClient(create_app(settings=settings, publisher=publisher, camera_service=camera_service)), publisher


def test_dashboard_endpoint_returns_frontend_payload_shape(tmp_path):
    client, _publisher = make_client(tmp_path)

    response = client.get("/api/dashboard")

    assert response.status_code == 200
    payload = response.json()
    assert "sensorData" in payload
    assert "deviceState" in payload
    assert "audioState" in payload
    assert "uiState" in payload
    assert "connectionState" in payload


def test_system_state_endpoint_returns_neutral_payload_shape(tmp_path):
    client, _publisher = make_client(tmp_path)

    response = client.get("/api/system-state")

    assert response.status_code == 200
    payload = response.json()
    assert set(payload) == {
        "updatedAtMs",
        "operationMode",
        "currentContext",
        "subsystems",
        "sensors",
        "devices",
        "connections",
    }
    assert payload["operationMode"]["smartModeEnabled"] is True
    assert payload["currentContext"]["scene"] == "vacant"


def test_app_exposes_system_state_for_internal_services(tmp_path):
    client, _publisher = make_client(tmp_path)

    snapshot = client.app.state.system_state.snapshot()

    assert snapshot["currentContext"]["scene"] == "vacant"


def test_app_exposes_fusion_and_gpio_services_for_internal_lifecycle(tmp_path):
    client, _publisher = make_client(tmp_path)

    assert client.app.state.fusion_control_service is not None
    assert client.app.state.gpio_button_service.status()["enabled"] is False


def test_fusion_control_status_endpoint_reports_gpio_button_state(tmp_path):
    client, _publisher = make_client(tmp_path)

    response = client.get("/api/fusion-control/status")

    assert response.status_code == 200
    payload = response.json()
    assert payload["phase"] == "vacant"
    assert payload["gpioButton"]["enabled"] is False


def test_app_exposes_music_service_for_internal_services(tmp_path):
    client, _publisher = make_client(tmp_path)

    assert client.app.state.music_service.snapshot()["connected"] is True


def test_app_exposes_camera_service_for_internal_services(tmp_path):
    camera_service = FakeCameraService()
    client, _publisher = make_client(tmp_path, camera_service=camera_service)

    assert client.app.state.camera_service is camera_service


def test_music_status_endpoint_returns_audio_state(tmp_path):
    client, _publisher = make_client(tmp_path)

    response = client.get("/api/music/status")

    assert response.status_code == 200
    assert response.json()["connected"] is True
    assert response.json()["playing"] is False


def test_camera_status_endpoint_returns_camera_state(tmp_path):
    frame = CameraFrame(content_type="image/jpeg", data=b"jpeg-bytes", captured_at_ms=1710000000000)
    client, _publisher = make_client(tmp_path, camera_service=FakeCameraService(frame))

    response = client.get("/api/camera/status")

    assert response.status_code == 200
    assert response.json() == {
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


def test_camera_snapshot_endpoint_returns_jpeg_bytes(tmp_path):
    frame = CameraFrame(content_type="image/jpeg", data=b"jpeg-bytes", captured_at_ms=1710000000000)
    client, _publisher = make_client(tmp_path, camera_service=FakeCameraService(frame))

    response = client.get("/api/camera/snapshot.jpg")

    assert response.status_code == 200
    assert response.headers["content-type"] == "image/jpeg"
    assert response.content == b"jpeg-bytes"


def test_camera_hls_proxy_forwards_query_string_to_upstream(tmp_path, monkeypatch):
    client, _publisher = make_client(tmp_path, camera_service=FakeCameraService())
    captured = {}

    class FakeHeaders:
        @staticmethod
        def get_content_type():
            return "application/vnd.apple.mpegurl"

    class FakeResponse:
        headers = FakeHeaders()

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb):
            return False

        def read(self):
            return b"#EXTM3U"

    def fake_urlopen(url):
        captured["url"] = url
        return FakeResponse()

    monkeypatch.setattr(app_module.urllib.request, "urlopen", fake_urlopen)

    response = client.get("/api/camera/hls/space/video1_stream.m3u8?session=abc123")

    assert response.status_code == 200
    assert captured["url"] == "http://127.0.0.1:8888/space/video1_stream.m3u8?session=abc123"


def test_music_command_endpoint_updates_audio_state(tmp_path):
    client, _publisher = make_client(tmp_path)

    response = client.post("/api/music/command", json={"action": "set_volume", "volume": 61})

    assert response.status_code == 200
    assert response.json()["ok"] is True
    assert client.app.state.system_state.snapshot()["devices"]["audio"]["volume"] == 61


def test_system_state_endpoint_includes_camera_contract(tmp_path):
    frame = CameraFrame(content_type="image/jpeg", data=b"jpeg-bytes", captured_at_ms=1710000000000)
    client, _publisher = make_client(tmp_path, camera_service=FakeCameraService(frame))

    response = client.get("/api/system-state")

    assert response.status_code == 200
    assert response.json()["subsystems"]["camera"]["status"] == "online"
    assert response.json()["devices"]["camera"] == {
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


def test_music_websocket_command_ack(tmp_path):
    client, _publisher = make_client(tmp_path)

    with client.websocket_connect("/ws/dashboard") as websocket:
        websocket.receive_json()
        websocket.send_json({"type": "music_command", "action": "set_volume", "volume": 66})
        payload = websocket.receive_json()

    assert payload["ok"] is True
    assert payload["type"] == "music_command_ack"
    assert payload["result"]["volume"] == 66


def test_smartlight_command_endpoint_publishes_fan_command(tmp_path):
    client, publisher = make_client(tmp_path)

    response = client.post("/api/smartlight/command", json={"action": "set_fan_level", "fanLevel": 7})

    assert response.status_code == 200
    assert response.json()["ok"] is True
    assert publisher.published
    name, topic, payload, qos = publisher.published[-1]
    assert name == "env"
    assert topic == "integration/smart/v1/env/rpi_env02/command"
    assert '"command": "set_fan_level"' in payload
    assert '"fan_level": 7' in payload
    assert qos == 1


def test_dashboard_command_syncs_operation_mode_to_system_state(tmp_path):
    client, _publisher = make_client(tmp_path)

    response = client.post("/api/smartlight/command", json={"action": "set_mode", "mode": "relax"})

    assert response.status_code == 200
    snapshot = client.app.state.system_state.snapshot()
    assert snapshot["operationMode"]["controlMode"] == "scene"
    assert snapshot["currentContext"]["scene"] == "relax"
    assert snapshot["currentContext"]["source"] == "dashboard"


def test_led_profile_api_updates_and_resets_user_profile(tmp_path):
    client, _publisher = make_client(tmp_path)

    update_response = client.post(
        "/api/led-profiles",
        json={"section": "scene_profiles", "key": "work", "patch": {"target_lux": 610, "tolerance_lux": 60}},
    )

    assert update_response.status_code == 200
    assert update_response.json()["scene_profiles"]["work"]["target_lux_min"] == 550
    assert update_response.json()["scene_profiles"]["work"]["target_lux_max"] == 670

    reset_response = client.post(
        "/api/led-profiles/reset",
        json={"section": "scene_profiles", "key": "work"},
    )

    assert reset_response.status_code == 200
    assert reset_response.json()["scene_profiles"]["work"]["target_lux"] == 500
