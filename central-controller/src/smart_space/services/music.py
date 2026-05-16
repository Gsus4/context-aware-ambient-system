from __future__ import annotations

import os
import random
import shutil
import signal
import subprocess
import threading
from collections.abc import Callable, Sequence
from pathlib import Path
from typing import Any, Protocol

from smart_space.core.settings import Settings


SUPPORTED_AUDIO_EXTENSIONS = {".mp3", ".flac", ".wav", ".m4a", ".aac", ".ogg", ".opus"}
PLAYABLE_SCENES = {"work", "relax", "sleep", "exercise"}
SCENE_ALIASES = {"work": ["work", "working"]}


class PlayerBackend(Protocol):
    def start(self, path: Path) -> Any: ...
    def pause(self, handle: Any) -> None: ...
    def resume(self, handle: Any) -> None: ...
    def stop(self, handle: Any) -> None: ...


class AudioController(Protocol):
    def current_volume(self) -> int | None: ...
    def set_volume(self, volume: int) -> None: ...
    def current_output(self) -> str | None: ...
    def set_output(self, output: str) -> None: ...
    def list_outputs(self) -> list[dict[str, str]]: ...


class FfplayBackend:
    def start(self, path: Path) -> subprocess.Popen:
        return subprocess.Popen(
            ["ffplay", "-nodisp", "-autoexit", "-hide_banner", "-loglevel", "error", str(path)],
            start_new_session=True,
        )

    def pause(self, handle: subprocess.Popen) -> None:
        if handle.poll() is None:
            os.killpg(handle.pid, signal.SIGSTOP)

    def resume(self, handle: subprocess.Popen) -> None:
        if handle.poll() is None:
            os.killpg(handle.pid, signal.SIGCONT)

    def stop(self, handle: subprocess.Popen) -> None:
        if handle.poll() is not None:
            return
        os.killpg(handle.pid, signal.SIGTERM)
        try:
            handle.wait(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(handle.pid, signal.SIGKILL)
            handle.wait(timeout=2)


class MemoryAudioController:
    def __init__(self, volume: int = 50, output: str = "default"):
        self._volume = _clamp_volume(volume)
        self._output = output

    def current_volume(self) -> int | None:
        return self._volume

    def set_volume(self, volume: int) -> None:
        self._volume = _clamp_volume(volume)

    def current_output(self) -> str | None:
        return self._output

    def set_output(self, output: str) -> None:
        if output:
            self._output = output

    def list_outputs(self) -> list[dict[str, str]]:
        return [{"id": self._output, "name": self._output}]


class NoopPlayerBackend:
    def start(self, path: Path) -> dict[str, str]:
        return {"path": str(path)}

    def pause(self, _handle: Any) -> None:
        return None

    def resume(self, _handle: Any) -> None:
        return None

    def stop(self, _handle: Any) -> None:
        return None


class SystemAudioController:
    def current_volume(self) -> int | None:
        if shutil.which("pactl"):
            completed = subprocess.run(
                ["pactl", "get-sink-volume", "@DEFAULT_SINK@"],
                check=False,
                text=True,
                capture_output=True,
            )
            if completed.returncode == 0:
                return _parse_percent(completed.stdout)
        if shutil.which("wpctl"):
            completed = subprocess.run(
                ["wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"],
                check=False,
                text=True,
                capture_output=True,
            )
            if completed.returncode == 0:
                return _parse_wpctl_volume(completed.stdout)
        return None

    def set_volume(self, volume: int) -> None:
        volume = _clamp_volume(volume)
        if shutil.which("pactl"):
            _run_audio_command(["pactl", "set-sink-volume", "@DEFAULT_SINK@", f"{volume}%"])
            return
        if shutil.which("wpctl"):
            _run_audio_command(["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", f"{volume / 100:.2f}"])
            return
        raise RuntimeError("audio_control_unavailable")

    def current_output(self) -> str | None:
        if shutil.which("pactl"):
            completed = subprocess.run(["pactl", "get-default-sink"], check=False, text=True, capture_output=True)
            if completed.returncode == 0:
                return completed.stdout.strip() or None
        return None

    def set_output(self, output: str) -> None:
        if not output:
            raise ValueError("missing_output_device")
        if shutil.which("pactl"):
            _run_audio_command(["pactl", "set-default-sink", output])
            return
        if shutil.which("wpctl"):
            _run_audio_command(["wpctl", "set-default", output])
            return
        raise RuntimeError("audio_control_unavailable")

    def list_outputs(self) -> list[dict[str, str]]:
        if shutil.which("pactl"):
            completed = subprocess.run(["pactl", "list", "short", "sinks"], check=False, text=True, capture_output=True)
            if completed.returncode == 0:
                outputs = []
                for line in completed.stdout.splitlines():
                    parts = line.split()
                    if len(parts) >= 2:
                        outputs.append({"id": parts[1], "name": parts[1]})
                return outputs
        return []


class LocalMusicService:
    def __init__(
        self,
        settings: Settings,
        *,
        player_backend: PlayerBackend | None = None,
        audio_controller: AudioController | None = None,
        chooser: Callable[[Sequence[Path]], Path] | None = None,
        on_state_change: Callable[[dict[str, Any]], None] | None = None,
    ):
        self.settings = settings
        self.music_dir = Path(settings.music_dir)
        self.player_backend = player_backend or FfplayBackend()
        self.audio_controller = audio_controller or SystemAudioController()
        self.chooser = chooser or random.choice
        self.on_state_change = on_state_change
        self._lock = threading.RLock()
        self._handle: Any | None = None
        self._tracks: list[Path] = []
        self._current_index = -1
        self._state: dict[str, Any] = {
            "connected": bool(settings.music_enabled),
            "playing": False,
            "state": "STOPPED",
            "mode": None,
            "scene": None,
            "autoModeEnabled": True,
            "volume": self._safe_current_volume(),
            "track": None,
            "trackPath": None,
            "outputDevice": self._safe_current_output(),
            "progressSec": None,
            "durationSec": None,
        }

    def start(self, *, ensure_dirs: bool = True) -> dict[str, Any]:
        if self.settings.music_enabled and ensure_dirs:
            self.ensure_scene_dirs()
        self._emit_state()
        return self.snapshot()

    def stop_service(self) -> dict[str, Any]:
        with self._lock:
            self._stop_locked()
            self._state["connected"] = False
            self._emit_state()
            return self.snapshot()

    def ensure_scene_dirs(self) -> None:
        for scene in sorted(PLAYABLE_SCENES):
            (self.music_dir / scene).mkdir(parents=True, exist_ok=True)

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            return dict(self._state)

    def scan_scene(self, scene: str) -> list[Path]:
        if scene == "vacant" or scene not in PLAYABLE_SCENES:
            return []
        for dirname in SCENE_ALIASES.get(scene, [scene]):
            folder = self.music_dir / dirname
            if not folder.is_dir():
                continue
            tracks = sorted(
                path
                for path in folder.iterdir()
                if path.is_file() and path.suffix.lower() in SUPPORTED_AUDIO_EXTENSIONS
            )
            if tracks:
                return tracks
        return []

    async def on_context_scene(self, scene: str) -> dict[str, Any]:
        if not self._state["autoModeEnabled"]:
            return {"ok": True, "reason": "auto_mode_disabled", "scene": scene}
        if scene == "vacant":
            with self._lock:
                self._state["scene"] = scene
                self._state["mode"] = scene
                self._stop_locked()
                self._emit_state()
            return {"ok": True, "reason": "vacant", "scene": scene}
        return await self.handle_command("play_scene", {"scene": scene})

    async def handle_command(self, command: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        params = params or {}
        with self._lock:
            if command == "get_status":
                return self.snapshot()
            if command == "set_auto_mode":
                self._state["autoModeEnabled"] = params.get("enabled") is not False
                self._emit_state()
                return self.snapshot()
            if command == "play_scene":
                return self._play_scene_locked(str(params.get("scene") or "work"))
            if command == "play":
                return self._play_track_locked(Path(str(params["trackPath"])))
            if command == "pause":
                return self._pause_locked()
            if command == "resume":
                return self._resume_locked()
            if command == "toggle":
                return self._pause_locked() if self._state["playing"] else self._resume_or_play_locked()
            if command == "stop":
                self._stop_locked()
                self._emit_state()
                return self.snapshot()
            if command == "next":
                return self._step_locked(+1)
            if command == "prev":
                return self._step_locked(-1)
            if command == "set_volume":
                volume = _clamp_volume(params.get("volume"))
                self.audio_controller.set_volume(volume)
                self._state["volume"] = volume
                self._emit_state()
                return self.snapshot()
            if command == "set_output":
                output = str(params.get("outputDevice") or params.get("output") or "")
                self.audio_controller.set_output(output)
                self._state["outputDevice"] = output
                if self._state["playing"] and self._state["trackPath"]:
                    self._play_track_locked(Path(self._state["trackPath"]), keep_playlist=True)
                else:
                    self._emit_state()
                return self.snapshot()
        raise ValueError("unsupported_music_action")

    def list_outputs(self) -> list[dict[str, str]]:
        return self.audio_controller.list_outputs()

    def _play_scene_locked(self, scene: str) -> dict[str, Any]:
        if scene == "vacant":
            self._state["scene"] = scene
            self._state["mode"] = scene
            self._stop_locked()
            self._emit_state()
            return {"ok": True, "reason": "vacant", "scene": scene}
        tracks = self.scan_scene(scene)
        if not tracks:
            self._tracks = []
            self._current_index = -1
            self._state["scene"] = scene
            self._state["mode"] = scene
            self._stop_locked()
            self._emit_state()
            return {"ok": False, "reason": "empty_scene", "scene": scene}
        selected = self.chooser(tracks)
        self._tracks = tracks
        self._current_index = tracks.index(selected)
        result = self._play_track_locked(selected, keep_playlist=True)
        result["scene"] = scene
        self._state["scene"] = scene
        self._state["mode"] = scene
        self._emit_state()
        return result

    def _play_track_locked(self, path: Path, *, keep_playlist: bool = False) -> dict[str, Any]:
        self._stop_locked()
        self._handle = self.player_backend.start(path)
        if not keep_playlist:
            self._tracks = [path]
            self._current_index = 0
        self._state.update({
            "connected": True,
            "playing": True,
            "state": "PLAYING",
            "track": path.name,
            "trackPath": str(path),
            "progressSec": None,
            "durationSec": None,
        })
        self._emit_state()
        return {"ok": True, **self.snapshot()}

    def _pause_locked(self) -> dict[str, Any]:
        if self._handle is not None and self._state["playing"]:
            self.player_backend.pause(self._handle)
            self._state["playing"] = False
            self._state["state"] = "PAUSED"
            self._emit_state()
        return self.snapshot()

    def _resume_or_play_locked(self) -> dict[str, Any]:
        if self._handle is not None and self._state["state"] == "PAUSED":
            return self._resume_locked()
        if self._state["trackPath"]:
            return self._play_track_locked(Path(self._state["trackPath"]), keep_playlist=True)
        if self._state["scene"]:
            return self._play_scene_locked(str(self._state["scene"]))
        return self.snapshot()

    def _resume_locked(self) -> dict[str, Any]:
        if self._handle is not None:
            self.player_backend.resume(self._handle)
            self._state["playing"] = True
            self._state["state"] = "PLAYING"
            self._emit_state()
        return self.snapshot()

    def _step_locked(self, direction: int) -> dict[str, Any]:
        if not self._tracks:
            return {"ok": False, "reason": "empty_playlist", **self.snapshot()}
        self._current_index = (self._current_index + direction) % len(self._tracks)
        return self._play_track_locked(self._tracks[self._current_index], keep_playlist=True)

    def _stop_locked(self) -> None:
        if self._handle is not None:
            self.player_backend.stop(self._handle)
            self._handle = None
        self._state["playing"] = False
        self._state["state"] = "STOPPED"

    def _safe_current_volume(self) -> int:
        try:
            value = self.audio_controller.current_volume()
        except Exception:
            value = None
        return _clamp_volume(value if value is not None else self.settings.music_default_volume)

    def _safe_current_output(self) -> str | None:
        try:
            return self.audio_controller.current_output()
        except Exception:
            return None

    def _emit_state(self) -> None:
        if self.on_state_change:
            self.on_state_change(self.snapshot())


def _clamp_volume(value: Any) -> int:
    try:
        number = int(round(float(value)))
    except (TypeError, ValueError):
        number = 50
    return max(0, min(100, number))


def _parse_percent(text: str) -> int | None:
    marker = "%"
    if marker not in text:
        return None
    before = text.split(marker, 1)[0]
    token = before.split()[-1]
    try:
        return _clamp_volume(token)
    except ValueError:
        return None


def _parse_wpctl_volume(text: str) -> int | None:
    for token in text.replace("[MUTED]", "").split():
        try:
            return _clamp_volume(float(token) * 100)
        except ValueError:
            continue
    return None


def _run_audio_command(command: list[str]) -> None:
    completed = subprocess.run(command, check=False, text=True, capture_output=True)
    if completed.returncode != 0:
        raise RuntimeError((completed.stderr or completed.stdout or "audio_command_failed").strip())
