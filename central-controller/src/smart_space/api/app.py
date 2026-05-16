from __future__ import annotations

import asyncio
import mimetypes
import socket
import urllib.error
import urllib.request
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse, Response
from fastapi.staticfiles import StaticFiles

from smart_space.core.settings import Settings
from smart_space.dashboard.profiles import LedProfileRepository
from smart_space.dashboard.state import DashboardState, Publisher, readable_error
from smart_space.mqtt.client import MqttClientManager
from smart_space.services.camera import SpaceCameraService
from smart_space.services.fusion_control import FusionControlService
from smart_space.services.interfaces import CameraService
from smart_space.services.music import LocalMusicService, MemoryAudioController, NoopPlayerBackend
from smart_space.services.scene_awareness import SceneAwarenessService
from smart_space.system_state import SystemStateManager


def create_app(
    settings: Settings | None = None,
    publisher: Publisher | None = None,
    camera_service: CameraService | None = None,
) -> FastAPI:
    settings = settings or Settings()
    system_state = SystemStateManager(settings)
    state = DashboardState(settings, publisher, system_state)
    profiles = LedProfileRepository(settings)
    mqtt_manager: MqttClientManager | None = None
    scene_listener = None

    def publish_music_state(music_state: dict[str, Any]) -> None:
        system_state.update_audio_state(music_state)
        state.mark_module("audio", "local music service")
        state.broadcast_patch({"audioState": music_state})

    music_service = LocalMusicService(
        settings,
        player_backend=NoopPlayerBackend() if publisher is not None else None,
        audio_controller=MemoryAudioController(settings.music_default_volume) if publisher is not None else None,
        on_state_change=publish_music_state,
    )

    def publish_camera_state(camera_state: dict[str, Any]) -> None:
        system_state.update_camera_state(camera_state)
        state.mark_module("camera", "space camera service")
        state.broadcast_patch({
            "sensorData": {
                "cameraSnapshotUrl": camera_state.get("snapshotUrl"),
                "cameraHlsUrl": camera_state.get("hlsUrl"),
                "cameraWebrtcUrl": camera_state.get("webrtcUrl"),
                "cameraWhepUrl": camera_state.get("whepUrl"),
                "cameraRtspUrl": camera_state.get("rtspUrl"),
                "cameraStreamPath": camera_state.get("streamPath"),
            },
        })

    resolved_camera_service = camera_service or SpaceCameraService(settings, on_state_change=publish_camera_state)
    publish_camera_state(resolved_camera_service.status())
    fusion_control_service = FusionControlService(settings, system_state, publisher=lambda: state.publisher)
    scene_awareness_service = SceneAwarenessService(
        settings,
        resolved_camera_service,
        system_state,
        fusion_control=fusion_control_service,
    )

    @asynccontextmanager
    async def lifespan(_app: FastAPI) -> AsyncIterator[None]:
        nonlocal mqtt_manager, scene_listener
        loop = asyncio.get_running_loop()

        def on_context_change(snapshot: dict[str, Any]) -> None:
            scene = snapshot["currentContext"]["scene"]

            async def apply_scene() -> None:
                try:
                    await music_service.on_context_scene(scene)
                except Exception as error:
                    state.broadcast_patch({"debugEvent": {"title": "Music scene update failed", "detail": str(error)}})

            loop.call_soon_threadsafe(lambda: asyncio.create_task(apply_scene()))

        scene_listener = on_context_change
        system_state.add_context_listener(scene_listener)
        if publisher is None:
            mqtt_manager = MqttClientManager(settings, state)
            state.publisher = mqtt_manager
            mqtt_manager.start()
        profiles.ensure_files()
        music_service.start(ensure_dirs=publisher is None)
        if hasattr(resolved_camera_service, "start"):
            camera_status = resolved_camera_service.start(host=_camera_public_host(settings))  # type: ignore[arg-type]
            publish_camera_state(camera_status)
        fusion_control_service.start(loop=loop)
        scene_awareness_service.start()
        try:
            yield
        finally:
            await scene_awareness_service.stop()
            fusion_control_service.stop()
            if scene_listener:
                system_state.remove_context_listener(scene_listener)
            music_service.stop_service()
            if hasattr(resolved_camera_service, "stop_service"):
                publish_camera_state(resolved_camera_service.stop_service())  # type: ignore[attr-defined]
            if mqtt_manager:
                mqtt_manager.stop()

    app = FastAPI(title="Smart Space Dashboard Backend", lifespan=lifespan)
    app.state.settings = settings
    app.state.dashboard_state = state
    app.state.system_state = system_state
    app.state.led_profiles = profiles
    app.state.music_service = music_service
    app.state.camera_service = resolved_camera_service
    app.state.fusion_control_service = fusion_control_service
    app.state.gpio_button_service = fusion_control_service.gpio_button_service
    app.state.scene_awareness_service = scene_awareness_service

    @app.exception_handler(ValueError)
    async def value_error_handler(_request: Request, error: ValueError):
        reason = str(error)
        return JSONResponse(status_code=400, content={"ok": False, "reason": readable_error(reason), "code": reason})

    @app.get("/api/dashboard")
    async def get_dashboard():
        return state.snapshot()

    @app.get("/api/system-state")
    async def get_system_state():
        return system_state.snapshot()

    @app.get("/api/led-profiles")
    async def get_led_profiles():
        return profiles.read()

    @app.post("/api/led-profiles")
    async def update_led_profiles(body: dict[str, Any]):
        return profiles.update(body.get("section", ""), body.get("key", ""), body.get("profile") or body.get("patch") or {})

    @app.post("/api/led-profiles/reset")
    async def reset_led_profiles(body: dict[str, Any]):
        return profiles.reset(body.get("section", ""), body.get("key", ""))

    @app.post("/api/smartlight/command")
    async def smartlight_command(body: dict[str, Any]):
        result = state.handle_dashboard_command(body)
        return {"ok": True, "result": result}

    @app.get("/api/music/status")
    async def music_status():
        return music_service.snapshot()

    @app.get("/api/camera/status")
    async def camera_status():
        return resolved_camera_service.status()

    @app.get("/api/fusion-control/status")
    async def fusion_control_status():
        return fusion_control_service.status()

    @app.get("/api/scene-awareness/status")
    async def scene_awareness_status():
        return scene_awareness_service.status()

    @app.get("/api/camera/snapshot.jpg")
    async def camera_snapshot():
        frame = await resolved_camera_service.latest_frame()
        if frame is None:
            raise HTTPException(status_code=404, detail="camera_snapshot_unavailable")
        return Response(content=frame.data, media_type=frame.content_type)

    @app.get("/api/camera/hls/{resource_path:path}")
    async def camera_hls_proxy(resource_path: str, request: Request):
        upstream = _camera_hls_upstream_url(settings, resource_path, str(request.query_params))
        try:
            with urllib.request.urlopen(upstream) as response:
                body = response.read()
                content_type = response.headers.get_content_type() or _guess_camera_media_type(resource_path)
        except urllib.error.HTTPError as error:
            raise HTTPException(status_code=error.code, detail="camera_hls_unavailable") from error
        except urllib.error.URLError as error:
            raise HTTPException(status_code=502, detail="camera_hls_unavailable") from error
        return Response(content=body, media_type=content_type)

    @app.get("/api/music/outputs")
    async def music_outputs():
        return {"ok": True, "outputs": music_service.list_outputs()}

    @app.post("/api/music/command")
    async def music_command(body: dict[str, Any]):
        result = await _handle_music_command(music_service, body)
        return {"ok": True, "result": result, **result}

    @app.websocket("/ws/dashboard")
    async def dashboard_ws(websocket: WebSocket):
        await websocket.accept()
        loop = asyncio.get_running_loop()
        queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue(maxsize=20)

        def listener(payload: dict[str, Any]) -> None:
            def enqueue() -> None:
                if queue.full():
                    try:
                        queue.get_nowait()
                    except asyncio.QueueEmpty:
                        pass
                queue.put_nowait(payload)

            loop.call_soon_threadsafe(enqueue)

        state.add_listener(listener)
        await websocket.send_json(state.snapshot())
        sender = asyncio.create_task(_send_ws_queue(websocket, queue))
        try:
            while True:
                data = await websocket.receive_json()
                if data.get("type") == "dashboard_command":
                    try:
                        result = state.handle_dashboard_command(data)
                        await websocket.send_json({"ok": True, "type": "dashboard_command_ack", "result": result})
                    except ValueError as error:
                        await websocket.send_json({"ok": False, "type": "dashboard_command_ack", "reason": readable_error(str(error)), "code": str(error)})
                elif data.get("type") == "music_command":
                    try:
                        result = await _handle_music_command(music_service, data)
                        await websocket.send_json({"ok": True, "type": "music_command_ack", "result": result})
                    except ValueError as error:
                        await websocket.send_json({"ok": False, "type": "music_command_ack", "reason": readable_error(str(error)), "code": str(error)})
                elif data.get("type") == "dashboard_visibility":
                    if data.get("visible") is True:
                        state.broadcast_patch({"connectionState": {"websocket": True}})
        except WebSocketDisconnect:
            pass
        finally:
            state.remove_listener(listener)
            sender.cancel()

    dashboard_dir = settings.dashboard_dir
    if dashboard_dir.exists():
        app.mount("/", StaticFiles(directory=dashboard_dir, html=True), name="dashboard")

    return app


async def _send_ws_queue(websocket: WebSocket, queue: asyncio.Queue[dict[str, Any]]) -> None:
    while True:
        payload = await queue.get()
        await websocket.send_json(payload)


async def _handle_music_command(music_service: LocalMusicService, body: dict[str, Any]) -> dict[str, Any]:
    action = str(body.get("action") or body.get("command") or "")
    if not action:
        raise ValueError("missing_music_action")
    try:
        return await music_service.handle_command(action, body)
    except (RuntimeError, KeyError) as error:
        raise ValueError(str(error)) from error


def _camera_hls_upstream_url(settings: Settings, resource_path: str, query_string: str = "") -> str:
    normalized = resource_path.lstrip("/")
    suffix = f"?{query_string}" if query_string else ""
    return f"http://127.0.0.1:{settings.camera_hls_port}/{normalized}{suffix}"


def _camera_public_host(settings: Settings) -> str:
    if settings.camera_public_host.strip():
        return settings.camera_public_host.strip()
    return socket.gethostname() or "127.0.0.1"


def _guess_camera_media_type(resource_path: str) -> str:
    guessed, _encoding = mimetypes.guess_type(resource_path)
    return guessed or "application/octet-stream"
