from __future__ import annotations

import shutil
import subprocess
import tempfile
import threading
import time
from collections.abc import Callable, Sequence
from pathlib import Path

from smart_space.core.settings import Settings
from smart_space.services.interfaces import CameraFrame


StatusCallback = Callable[[dict[str, object]], None]
SpawnProcess = Callable[[Sequence[str], Path | None], subprocess.Popen]
CaptureStill = Callable[[Sequence[str]], bytes]


class SpaceCameraService:
    def __init__(
        self,
        settings: Settings,
        *,
        command_exists: Callable[[str], str | None] | None = None,
        spawn_process: SpawnProcess | None = None,
        capture_still: CaptureStill | None = None,
        on_state_change: StatusCallback | None = None,
    ):
        self.settings = settings
        self.command_exists = command_exists or shutil.which
        self.spawn_process = spawn_process or _spawn_process
        self.capture_still = capture_still or _capture_still
        self.on_state_change = on_state_change
        self._lock = threading.RLock()
        self._snapshot_thread: threading.Thread | None = None
        self._snapshot_stop = threading.Event()
        self._mediamtx_process: subprocess.Popen | None = None
        self._frame: CameraFrame | None = None
        self._runtime_config_dir: Path | None = None
        self._runtime_config_path: Path | None = None
        self._status: dict[str, object] = {
            "connected": None,
            "streamPath": settings.camera_stream_path,
            "rtspUrl": None,
            "webrtcUrl": None,
            "whepUrl": None,
            "hlsUrl": self._proxy_hls_url(),
            "snapshotUrl": "/api/camera/snapshot.jpg",
            "lastFrameAtMs": None,
            "lastError": None,
        }

    def start(self, *, host: str = "127.0.0.1") -> dict[str, object]:
        with self._lock:
            if not self.settings.camera_enabled:
                self._status.update({
                    "connected": False,
                    "rtspUrl": self._rtsp_url(host),
                    "webrtcUrl": self._webrtc_url(host),
                    "whepUrl": self._whep_url(host),
                    "hlsUrl": self._proxy_hls_url(),
                    "lastError": None,
                })
                self._emit_state()
                return self.status()

            self._status.update({
                "streamPath": self.settings.camera_stream_path,
                "rtspUrl": self._rtsp_url(host),
                "webrtcUrl": self._webrtc_url(host),
                "whepUrl": self._whep_url(host),
                "hlsUrl": self._proxy_hls_url(),
                "snapshotUrl": "/api/camera/snapshot.jpg",
                "lastError": None,
            })
            try:
                mediamtx_bin = self.settings.camera_mediamtx_bin
                mediamtx_config = self._prepare_runtime_config_locked()
                repo_root = _repo_root(self.settings.camera_mediamtx_config)
                self._mediamtx_process = self.spawn_process(
                    [str(mediamtx_bin), str(mediamtx_config)],
                    repo_root,
                )
                self._status["connected"] = True
                self._capture_snapshot_locked()
                self._start_snapshot_loop_locked()
            except Exception as error:
                self._status["connected"] = False
                self._status["lastError"] = str(error)
                self._stop_process_locked("_mediamtx_process")
            self._emit_state()
            return self.status()

    def stop_service(self) -> dict[str, object]:
        with self._lock:
            self._snapshot_stop.set()
            self._stop_process_locked("_mediamtx_process")
            self._cleanup_runtime_config_locked()
            self._status["connected"] = False
            self._emit_state()
            return self.status()

    async def latest_frame(self) -> CameraFrame | None:
        with self._lock:
            return self._frame

    def status(self) -> dict[str, object]:
        with self._lock:
            return dict(self._status)

    def _start_snapshot_loop_locked(self) -> None:
        if self._snapshot_thread and self._snapshot_thread.is_alive():
            return
        self._snapshot_stop.clear()
        self._snapshot_thread = threading.Thread(target=self._snapshot_loop, name="space-camera-snapshot", daemon=True)
        self._snapshot_thread.start()

    def _snapshot_loop(self) -> None:
        interval = max(0.25, self.settings.camera_snapshot_interval_ms / 1000)
        while not self._snapshot_stop.wait(interval):
            with self._lock:
                if self._status.get("connected") is not True:
                    continue
                self._capture_snapshot_locked()
                self._emit_state()

    def _capture_snapshot_locked(self) -> None:
        errors = []
        try:
            for command in self._snapshot_commands():
                try:
                    image = self.capture_still(command)
                    captured_at_ms = round(time.time() * 1000)
                    self._frame = CameraFrame(content_type="image/jpeg", data=image, captured_at_ms=captured_at_ms)
                    self._status["lastFrameAtMs"] = captured_at_ms
                    self._status["lastError"] = None
                    return
                except Exception as error:
                    errors.append(f"{command[0]}: {error}")
            self._status["lastError"] = "; ".join(errors) or "camera_snapshot_unavailable"
        except Exception as error:
            self._status["lastError"] = str(error)

    def _snapshot_commands(self) -> list[list[str]]:
        commands = []
        if self.command_exists("ffmpeg"):
            commands.append(self._rtsp_snapshot_command())
        commands.append(self._still_command())
        return commands

    def _rtsp_snapshot_command(self) -> list[str]:
        return [
            "ffmpeg",
            "-hide_banner",
            "-loglevel", "error",
            "-rtsp_transport", "tcp",
            "-i", self._local_rtsp_url(),
            "-frames:v", "1",
            "-f", "image2pipe",
            "-vcodec", "mjpeg",
            "-",
        ]

    def _still_command(self) -> list[str]:
        width = str(self.settings.camera_width)
        height = str(self.settings.camera_height)
        if self.command_exists("rpicam-still"):
            return [
                "rpicam-still",
                "--nopreview",
                "--immediate",
                "--width", width,
                "--height", height,
                "--encoding", "jpg",
                "--output", "-",
            ]
        if self.command_exists("libcamera-still"):
            return [
                "libcamera-still",
                "--nopreview",
                "--immediate",
                "--width", width,
                "--height", height,
                "--encoding", "jpg",
                "--output", "-",
            ]
        raise RuntimeError("camera_snapshot_unavailable")

    def _rtsp_url(self, host: str) -> str:
        resolved_host = _url_host(host)
        return f"rtsp://{resolved_host}:{self.settings.camera_rtsp_port}/{self.settings.camera_stream_path}"

    def _local_rtsp_url(self) -> str:
        return f"rtsp://127.0.0.1:{self.settings.camera_rtsp_port}/{self.settings.camera_stream_path}"

    def _hls_url(self, host: str) -> str:
        resolved_host = _url_host(host)
        return f"http://{resolved_host}:{self.settings.camera_hls_port}/{self.settings.camera_stream_path}/index.m3u8"

    def _webrtc_url(self, host: str) -> str:
        resolved_host = _url_host(host)
        return f"http://{resolved_host}:{self.settings.camera_webrtc_port}/{self.settings.camera_stream_path}"

    def _whep_url(self, host: str) -> str:
        return f"{self._webrtc_url(host)}/whep"

    def _proxy_hls_url(self) -> str:
        return f"/api/camera/hls/{self.settings.camera_stream_path}/index.m3u8"

    def _emit_state(self) -> None:
        if self.on_state_change:
            self.on_state_change(self.status())

    def _prepare_runtime_config_locked(self) -> Path:
        template = self.settings.camera_mediamtx_config.read_text(encoding="utf-8")
        if "\npaths:\n" not in template:
            raise RuntimeError("camera_mediamtx_config_invalid")
        prefix = template.split("\npaths:\n", 1)[0]
        runtime_body = "\n".join([
            "paths:",
            f"  {self.settings.camera_stream_path}:",
            "    source: rpiCamera",
            f"    rpiCameraWidth: {self.settings.camera_width}",
            f"    rpiCameraHeight: {self.settings.camera_height}",
            f"    rpiCameraFPS: {self.settings.camera_fps}",
            f"    rpiCameraIDRPeriod: {max(1, self.settings.camera_fps // 2)}",
            f"    rpiCameraBitrate: {self.settings.camera_bitrate}",
            "  all_others:",
            "",
        ])
        runtime_dir = Path(tempfile.mkdtemp(prefix="smart-space-camera-"))
        runtime_path = runtime_dir / "mediamtx.runtime.yml"
        runtime_path.write_text(prefix + "\npaths:\n" + runtime_body.split("paths:\n", 1)[1], encoding="utf-8")
        self._runtime_config_dir = runtime_dir
        self._runtime_config_path = runtime_path
        return runtime_path

    def _stop_process_locked(self, attr_name: str) -> None:
        process = getattr(self, attr_name)
        if process is None:
            return
        try:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=2)
        except Exception:
            try:
                process.kill()
                process.wait(timeout=2)
            except Exception:
                pass
        finally:
            setattr(self, attr_name, None)

    def _cleanup_runtime_config_locked(self) -> None:
        if self._runtime_config_path and self._runtime_config_path.exists():
            self._runtime_config_path.unlink(missing_ok=True)
        if self._runtime_config_dir and self._runtime_config_dir.exists():
            self._runtime_config_dir.rmdir()
        self._runtime_config_path = None
        self._runtime_config_dir = None


def _repo_root(config_path: Path) -> Path:
    resolved = config_path.resolve()
    if resolved.parent.name == "mediamtx":
        return resolved.parent.parent
    return resolved.parent


def _url_host(host: str) -> str:
    value = (host or "").strip()
    if value in {"", "0.0.0.0", "::"}:
        return "127.0.0.1"
    return value


def _spawn_process(command: Sequence[str], cwd: Path | None = None) -> subprocess.Popen:
    return subprocess.Popen(command, cwd=str(cwd) if cwd else None, start_new_session=True)


def _capture_still(command: Sequence[str]) -> bytes:
    completed = subprocess.run(command, check=True, capture_output=True, timeout=8)
    return completed.stdout
