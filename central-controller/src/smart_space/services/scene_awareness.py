from __future__ import annotations

import asyncio
import json
import time
from collections import deque
from collections.abc import Callable, Sequence
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Protocol

from smart_space.core.settings import Settings
from smart_space.services.interfaces import CameraFrame, CameraService
from smart_space.system_state import SystemStateManager


SCENE_STATE_TO_CONTEXT = {
    "EMPTY": "vacant",
    "SLEEPING": "sleep",
    "WORKING": "work",
    "RELAXING": "relax",
    "EXERCISING": "exercise",
}
REQUIRED_FRAME_COUNT = 6


class SceneClassifier(Protocol):
    async def classify(self, frames: Sequence[CameraFrame], state: dict[str, Any]) -> tuple[dict[str, Any], float]: ...


class FusionControl(Protocol):
    async def apply_observation(self, observation: dict[str, Any]) -> dict[str, Any]: ...


def load_system_prompt(path: str | Path) -> str:
    return Path(path).read_text(encoding="utf-8").strip()


def resolve_google_api_key(settings: Settings) -> str:
    configured = settings.google_api_key.strip()
    if configured:
        return configured
    fallback = settings.scene_awareness_env_fallback_path
    if not fallback.exists():
        return ""
    for line in fallback.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        if key.strip() == "GOOGLE_API_KEY":
            return value.strip().strip('"').strip("'")
    return ""


def parse_scene_response(text: str) -> dict[str, Any]:
    cleaned = text.strip()
    if cleaned.startswith("```"):
        lines = cleaned.splitlines()
        if lines and lines[0].startswith("```"):
            lines = lines[1:]
        if lines and lines[-1].startswith("```"):
            lines = lines[:-1]
        cleaned = "\n".join(lines).strip()

    parsed = json.loads(cleaned)
    if not isinstance(parsed, dict):
        raise ValueError("scene response must be a JSON object")
    return parsed


def read_recent_log_lines(path: str | Path, *, limit: int = 80) -> list[str]:
    log_path = Path(path)
    if not log_path.exists():
        return []
    lines = log_path.read_text(encoding="utf-8").splitlines()
    return lines[-max(0, limit):]


class GoogleSceneClassifier:
    def __init__(
        self,
        *,
        client: Any,
        part_factory: Any,
        model: str,
        system_prompt: str,
        monotonic: Callable[[], float] | None = None,
    ) -> None:
        self.client = client
        self.part_factory = part_factory
        self.model = model
        self.system_prompt = system_prompt
        self.monotonic = monotonic or time.monotonic

    async def classify(self, frames: Sequence[CameraFrame], state: dict[str, Any]) -> tuple[dict[str, Any], float]:
        return await asyncio.to_thread(self.classify_sync, frames, state)

    def classify_sync(self, frames: Sequence[CameraFrame], state: dict[str, Any]) -> tuple[dict[str, Any], float]:
        contents: list[Any] = [self.system_prompt, self._context_prompt(frames, state)]
        for item in frames:
            contents.append(self.part_factory.from_bytes(data=item.data, mime_type=item.content_type))

        started_at = self.monotonic()
        response = self.client.models.generate_content(model=self.model, contents=contents)
        elapsed_seconds = self.monotonic() - started_at
        if not response.text:
            raise RuntimeError("empty response from scene classifier")
        return parse_scene_response(response.text), elapsed_seconds

    @staticmethod
    def _context_prompt(frames: Sequence[CameraFrame], state: dict[str, Any]) -> str:
        heart_rate = state.get("sensors", {}).get("heartRate")
        payload = {
            "frame_sequence": [
                {
                    "index": index + 1,
                    "captured_at_ms": frame.captured_at_ms,
                }
                for index, frame in enumerate(frames)
            ],
            "heart_rate_bpm": heart_rate,
        }
        return "Runtime context JSON:\n" + json.dumps(payload, ensure_ascii=False, sort_keys=True)


class SceneAwarenessService:
    def __init__(
        self,
        settings: Settings,
        camera_service: CameraService,
        system_state: SystemStateManager,
        *,
        classifier: SceneClassifier | None = None,
        fusion_control: FusionControl | None = None,
        infer_now: Callable[[], float] | None = None,
    ) -> None:
        self.settings = settings
        self.camera_service = camera_service
        self.system_state = system_state
        self.infer_now = infer_now or time.monotonic
        self._frames: deque[CameraFrame] = deque(maxlen=REQUIRED_FRAME_COUNT)
        self._task: asyncio.Task[None] | None = None
        self._inference_task: asyncio.Task[None] | None = None
        self._stop = asyncio.Event()
        self._last_request_started_at: float | None = None
        self._last_smart_mode_enabled: bool | None = None
        self._classifier = classifier
        self._fusion_control = fusion_control
        self._status: dict[str, Any] = {
            "enabled": bool(settings.scene_awareness_enabled),
            "running": False,
            "reason": "",
            "model": settings.scene_awareness_model,
            "bufferSize": 0,
            "requiredFrames": REQUIRED_FRAME_COUNT,
            "captureIntervalSeconds": settings.scene_awareness_capture_interval_seconds,
            "inferIntervalSeconds": settings.scene_awareness_infer_interval_seconds,
            "requestsStarted": 0,
            "requestsCompleted": 0,
            "requestsFailed": 0,
            "requestsSkipped": 0,
            "requestsDiscarded": 0,
            "lastResponseTimeSeconds": None,
            "lastResult": None,
            "lastError": None,
        }
        self._initialize_classifier()

    def start(self) -> None:
        self._reset_log()
        if not self._status["enabled"] or self._task:
            self._log_event("service_disabled", reason=self._status.get("reason") or "disabled")
            return
        self._stop.clear()
        self._status["running"] = True
        self._log_event(
            "service_start",
            model=self.settings.scene_awareness_model,
            capture_interval_seconds=self.settings.scene_awareness_capture_interval_seconds,
            infer_interval_seconds=self.settings.scene_awareness_infer_interval_seconds,
        )
        self._task = asyncio.create_task(self._run_loop(), name="scene-awareness")

    async def stop(self) -> None:
        self._stop.set()
        tasks = [task for task in (self._task, self._inference_task) if task]
        for task in tasks:
            task.cancel()
        for task in tasks:
            try:
                await task
            except asyncio.CancelledError:
                pass
        self._task = None
        self._inference_task = None
        self._status["running"] = False
        self._log_event("service_stop")

    def status(self) -> dict[str, Any]:
        self._status["bufferSize"] = len(self._frames)
        return dict(self._status)

    async def collect_once(self) -> None:
        smart_mode_enabled = self._smart_mode_enabled()
        self._log_smart_mode_transition(smart_mode_enabled)

        frame = await self.camera_service.latest_frame()
        if frame is None:
            self._status["lastError"] = "camera_frame_unavailable"
            self._status["bufferSize"] = len(self._frames)
            self._log_event("camera_frame_unavailable", buffer_size=len(self._frames), camera_status=self.camera_service.status())
            return

        self._frames.append(frame)
        self._status["bufferSize"] = len(self._frames)
        self._log_event("frame_captured", buffer_size=len(self._frames), captured_at_ms=frame.captured_at_ms)
        if not smart_mode_enabled:
            self._log_event("inference_paused", reason="smart_mode_disabled", buffer_size=len(self._frames))
            return
        if self._context_is_vacant():
            self._log_event("inference_paused", reason="context_vacant", buffer_size=len(self._frames))
            return
        if len(self._frames) < REQUIRED_FRAME_COUNT:
            return
        await self._maybe_start_inference()

    async def _run_loop(self) -> None:
        try:
            while not self._stop.is_set():
                await self.collect_once()
                try:
                    await asyncio.wait_for(
                        self._stop.wait(),
                        timeout=max(0.1, self.settings.scene_awareness_capture_interval_seconds),
                    )
                except TimeoutError:
                    pass
        finally:
            self._status["running"] = False

    async def _maybe_start_inference(self) -> None:
        now = self.infer_now()
        if self._last_request_started_at is not None and now - self._last_request_started_at < self.settings.scene_awareness_infer_interval_seconds:
            return
        if self._inference_task and not self._inference_task.done():
            self._status["requestsSkipped"] += 1
            self._last_request_started_at = now
            self._log_event("request_skipped", reason="inference_in_progress")
            return
        self._last_request_started_at = now
        frames = list(self._frames)
        state = self.system_state.snapshot()
        request_started_at_ms = round(time.time() * 1000)
        self._status["requestsStarted"] += 1
        request_id = self._status["requestsStarted"]
        self._log_event(
            "request_start",
            request_id=request_id,
            request_started_at_ms=request_started_at_ms,
            frame_count=len(frames),
            frame_captured_at_ms=[frame.captured_at_ms for frame in frames],
            heart_rate_bpm=state.get("sensors", {}).get("heartRate"),
            request_context_scene=state.get("currentContext", {}).get("scene"),
        )
        self._inference_task = asyncio.create_task(
            self._run_inference(frames, state, request_started_at_ms),
            name="scene-awareness-infer",
        )

    async def _run_inference(self, frames: list[CameraFrame], state: dict[str, Any], request_started_at_ms: int) -> None:
        if not self._classifier:
            return
        request_id = self._status["requestsStarted"]
        try:
            result, elapsed_seconds = await self._classifier.classify(frames, state)
            normalized_result = self._normalize_result(result, elapsed_seconds, state, request_started_at_ms)
            self._status["lastResult"] = normalized_result
            self._status["lastResponseTimeSeconds"] = elapsed_seconds
            self._status["lastError"] = None
            self._log_event(
                "response_received",
                request_id=request_id,
                elapsed_seconds=elapsed_seconds,
                response=normalized_result,
            )
            if not self._inference_enabled():
                self._status["requestsDiscarded"] += 1
                reason = "smart_mode_disabled" if not self._smart_mode_enabled() else "context_vacant"
                self._log_event("response_discarded", request_id=request_id, reason=reason)
                return
            if self._fusion_control:
                fusion_result = await self._fusion_control.apply_observation(normalized_result)
                self._log_event("fusion_control_applied", request_id=request_id, result=fusion_result)
            self._status["requestsCompleted"] += 1
        except Exception as error:
            self._status["requestsFailed"] += 1
            self._status["lastError"] = str(error)
            self._log_event("request_failed", request_id=request_id, error=str(error))

    def _normalize_result(
        self,
        result: dict[str, Any],
        elapsed_seconds: float,
        request_state: dict[str, Any],
        request_started_at_ms: int,
    ) -> dict[str, Any]:
        scene_state = str(result.get("scene_state") or "UNKNOWN").strip().upper()
        return {
            **result,
            "scene_state": scene_state,
            "mapped_scene": SCENE_STATE_TO_CONTEXT.get(scene_state),
            "response_time_seconds": elapsed_seconds,
            "request_started_at_ms": request_started_at_ms,
            "request_context_scene": request_state.get("currentContext", {}).get("scene"),
            "request_context_source": request_state.get("currentContext", {}).get("source"),
            "request_context_updated_at_ms": request_state.get("updatedAtMs"),
        }

    def _smart_mode_enabled(self) -> bool:
        snapshot = self.system_state.snapshot()
        return snapshot.get("operationMode", {}).get("smartModeEnabled") is not False

    def _context_is_vacant(self) -> bool:
        snapshot = self.system_state.snapshot()
        return snapshot.get("currentContext", {}).get("scene") == "vacant"

    def _inference_enabled(self) -> bool:
        snapshot = self.system_state.snapshot()
        return (
            snapshot.get("operationMode", {}).get("smartModeEnabled") is not False
            and snapshot.get("currentContext", {}).get("scene") != "vacant"
        )

    def _initialize_classifier(self) -> None:
        if not self.settings.scene_awareness_enabled:
            self._status["enabled"] = False
            self._status["reason"] = "disabled_by_settings"
            return
        if self._classifier:
            self._status["reason"] = "injected_classifier"
            return

        api_key = resolve_google_api_key(self.settings)
        if not api_key:
            self._status["enabled"] = False
            self._status["reason"] = "missing_google_api_key"
            return
        try:
            from google import genai
            from google.genai import types

            self._classifier = GoogleSceneClassifier(
                client=genai.Client(api_key=api_key),
                part_factory=types.Part,
                model=self.settings.scene_awareness_model,
                system_prompt=load_system_prompt(self.settings.scene_awareness_prompt_path),
            )
            self._status["reason"] = "ready"
        except Exception as error:
            self._status["enabled"] = False
            self._status["reason"] = "classifier_initialization_failed"
            self._status["lastError"] = str(error)

    def _reset_log(self) -> None:
        log_path = self.settings.scene_awareness_log_path
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text("", encoding="utf-8")

    def _log_smart_mode_transition(self, smart_mode_enabled: bool) -> None:
        if self._last_smart_mode_enabled is smart_mode_enabled:
            return
        self._last_smart_mode_enabled = smart_mode_enabled
        self._log_event("smart_mode_enabled" if smart_mode_enabled else "smart_mode_disabled")

    def _log_event(self, event: str, **fields: Any) -> None:
        log_path = self.settings.scene_awareness_log_path
        log_path.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "ts": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "event": event,
            **fields,
        }
        with log_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n")
