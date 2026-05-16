from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Protocol


@dataclass(frozen=True)
class CameraFrame:
    content_type: str
    data: bytes
    captured_at_ms: int


@dataclass(frozen=True)
class SceneObservation:
    scene: str
    confidence: float
    reason: str
    raw: dict[str, Any]


@dataclass(frozen=True)
class ControlDecision:
    light: dict[str, Any] | None = None
    fan: dict[str, Any] | None = None
    music: dict[str, Any] | None = None
    reason: str = ""


class CameraService(Protocol):
    async def latest_frame(self) -> CameraFrame | None: ...
    def status(self) -> dict[str, Any]: ...


class ContextAwarenessService(Protocol):
    async def infer_scene(self, frame: CameraFrame | None, state: dict[str, Any]) -> SceneObservation: ...


class FusionControlService(Protocol):
    async def decide(self, observation: SceneObservation, state: dict[str, Any]) -> ControlDecision: ...


class MusicService(Protocol):
    async def handle_command(self, command: str, params: dict[str, Any]) -> dict[str, Any]: ...


class EventRepository(Protocol):
    async def append(self, event_type: str, payload: dict[str, Any]) -> None: ...
