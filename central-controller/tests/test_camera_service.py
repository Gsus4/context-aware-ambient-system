import asyncio
from pathlib import Path

from smart_space.core.settings import Settings
from smart_space.services.camera import SpaceCameraService


class FakeProcess:
    def __init__(self):
        self.terminated = False
        self.killed = False
        self.returncode = None

    def poll(self):
        return self.returncode

    def terminate(self):
        self.terminated = True
        self.returncode = 0

    def kill(self):
        self.killed = True
        self.returncode = -9

    def wait(self, timeout=None):
        self.returncode = 0
        return 0


def make_settings(tmp_path):
    return Settings(
        port=3000,
        mqtt_host="localhost",
        wearable_mqtt_host="localhost",
        env_mqtt_host="env-broker.local",
        dashboard_dir=Path("src/smart_space/web/dashboard"),
        led_profile_default_path=Path("src/smart_space/config/led_profiles.default.json"),
        led_profile_user_path=tmp_path / "led_profiles.user.json",
        camera_enabled=True,
        camera_stream_path="space",
        camera_mediamtx_bin=Path("mediamtx/mediamtx"),
        camera_mediamtx_config=Path("mediamtx/mediamtx.yml"),
    )


def test_camera_service_prefers_rpicam_stack_and_computes_urls(tmp_path):
    calls = []

    def spawn(command, cwd=None):
        calls.append((command, cwd))
        return FakeProcess()

    service = SpaceCameraService(
        make_settings(tmp_path),
        command_exists=lambda name: name in {"rpicam-still"},
        spawn_process=spawn,
        capture_still=lambda command: b"jpeg-bytes",
    )

    status = service.start(host="127.0.0.1")

    assert status["connected"] is True
    assert status["streamPath"] == "space"
    assert status["rtspUrl"] == "rtsp://127.0.0.1:8554/space"
    assert status["webrtcUrl"] == "http://127.0.0.1:8889/space"
    assert status["whepUrl"] == "http://127.0.0.1:8889/space/whep"
    assert status["hlsUrl"] == "/api/camera/hls/space/index.m3u8"
    assert calls[0][0][0] == "mediamtx/mediamtx"
    runtime_config = Path(calls[0][0][1])
    config_text = runtime_config.read_text(encoding="utf-8")
    assert "space:" in config_text
    assert "source: rpiCamera" in config_text
    assert "rpiCameraWidth: 960" in config_text
    assert "rpiCameraHeight: 720" in config_text
    assert "rpiCameraIDRPeriod: 12" in config_text
    assert len(calls) == 1


def test_camera_service_uses_snapshot_fallback_commands_independently_of_stream_source(tmp_path):
    calls = []

    def spawn(command, cwd=None):
        calls.append((command, cwd))
        return FakeProcess()

    service = SpaceCameraService(
        make_settings(tmp_path),
        command_exists=lambda name: name in {"libcamera-still"},
        spawn_process=spawn,
        capture_still=lambda command: b"jpeg-bytes",
    )

    service.start(host="127.0.0.1")

    frame = asyncio.run(service.latest_frame())

    assert frame is not None
    assert calls[0][0][0] == "mediamtx/mediamtx"


def test_camera_service_latest_frame_returns_cached_snapshot(tmp_path):
    service = SpaceCameraService(
        make_settings(tmp_path),
        command_exists=lambda name: name in {"rpicam-vid", "rpicam-still"},
        spawn_process=lambda command, cwd=None: FakeProcess(),
        capture_still=lambda command: b"jpeg-bytes",
    )

    service.start(host="127.0.0.1")

    frame = asyncio.run(service.latest_frame())

    assert frame is not None
    assert frame.content_type == "image/jpeg"
    assert frame.data == b"jpeg-bytes"


def test_camera_service_prefers_rtsp_snapshot_when_ffmpeg_is_available(tmp_path):
    commands = []

    def capture(command):
        commands.append(command)
        return b"rtsp-jpeg"

    service = SpaceCameraService(
        make_settings(tmp_path),
        command_exists=lambda name: name in {"ffmpeg", "rpicam-still"},
        spawn_process=lambda command, cwd=None: FakeProcess(),
        capture_still=capture,
    )

    service.start(host="s10RPi4")
    frame = asyncio.run(service.latest_frame())

    assert frame is not None
    assert frame.data == b"rtsp-jpeg"
    assert commands[0][0] == "ffmpeg"
    assert "rtsp://127.0.0.1:8554/space" in commands[0]


def test_camera_service_falls_back_to_still_snapshot_when_rtsp_capture_fails(tmp_path):
    commands = []

    def capture(command):
        commands.append(command)
        if command[0] == "ffmpeg":
            raise RuntimeError("rtsp unavailable")
        return b"still-jpeg"

    service = SpaceCameraService(
        make_settings(tmp_path),
        command_exists=lambda name: name in {"ffmpeg", "rpicam-still"},
        spawn_process=lambda command, cwd=None: FakeProcess(),
        capture_still=capture,
    )

    service.start(host="127.0.0.1")
    frame = asyncio.run(service.latest_frame())

    assert frame is not None
    assert frame.data == b"still-jpeg"
    assert commands[0][0] == "ffmpeg"
    assert commands[1][0] == "rpicam-still"
