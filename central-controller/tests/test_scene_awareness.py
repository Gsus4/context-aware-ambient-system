import asyncio
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from smart_space.api.app import create_app
from smart_space.core.settings import Settings
from smart_space.services.interfaces import CameraFrame
from smart_space.services.scene_awareness import (
    SCENE_STATE_TO_CONTEXT,
    GoogleSceneClassifier,
    SceneAwarenessService,
    read_recent_log_lines,
    parse_scene_response,
    resolve_google_api_key,
)
from smart_space.system_state import SystemStateManager


class FakeCameraService:
    def __init__(self, frames):
        self.frames = list(frames)
        self.index = 0

    async def latest_frame(self):
        if not self.frames:
            return None
        frame = self.frames[min(self.index, len(self.frames) - 1)]
        self.index += 1
        return frame

    def status(self):
        return {"connected": bool(self.frames)}


class FakeClassifier:
    def __init__(self, response=None, wait_for=None):
        self.response = response or {
            "scene_state": "WORKING",
            "confidence": 0.82,
            "reason": "person is focused at a desk",
        }
        self.wait_for = wait_for
        self.calls = []

    async def classify(self, frames, state):
        self.calls.append((list(frames), state))
        if self.wait_for:
            await self.wait_for.wait()
        return self.response, 0.25


class FakeFusionControl:
    def __init__(self):
        self.calls = []

    async def apply_observation(self, observation):
        self.calls.append(dict(observation))
        return {"applied": True}


def make_settings(tmp_path, **overrides):
    values = {
        "port": 0,
        "mqtt_host": "localhost",
        "wearable_mqtt_host": "localhost",
        "env_mqtt_host": "env-broker.local",
        "dashboard_dir": Path("src/smart_space/web/dashboard"),
        "led_profile_default_path": Path("src/smart_space/config/led_profiles.default.json"),
        "led_profile_user_path": tmp_path / "led_profiles.user.json",
        "camera_enabled": False,
        "scene_awareness_enabled": True,
        "google_api_key": "",
        "scene_awareness_capture_interval_seconds": 5,
        "scene_awareness_infer_interval_seconds": 30,
        "scene_awareness_prompt_path": tmp_path / "scene_prompt.txt",
        "scene_awareness_env_fallback_path": tmp_path / "missing.env",
        "scene_awareness_log_path": tmp_path / "scene_awareness.log",
    }
    values.update(overrides)
    values["scene_awareness_prompt_path"].write_text("prompt", encoding="utf-8")
    return Settings(**values)


def frame(seq):
    return CameraFrame(content_type="image/jpeg", data=f"jpeg-{seq}".encode(), captured_at_ms=1710000000000 + seq)


def test_parse_scene_response_accepts_fenced_json():
    parsed = parse_scene_response('```json\n{"scene_state":"WORKING","confidence":0.7}\n```')

    assert parsed == {"scene_state": "WORKING", "confidence": 0.7}


def test_parse_scene_response_rejects_non_object_json():
    with pytest.raises(ValueError, match="scene response must be a JSON object"):
        parse_scene_response('["WORKING"]')


def test_scene_state_mapping_matches_context_contract():
    assert SCENE_STATE_TO_CONTEXT == {
        "EMPTY": "vacant",
        "SLEEPING": "sleep",
        "WORKING": "work",
        "RELAXING": "relax",
        "EXERCISING": "exercise",
    }


def test_google_classifier_payload_includes_six_images_and_heart_rate_not_spo2(tmp_path):
    captured = {}

    class FakePart:
        @staticmethod
        def from_bytes(data, mime_type):
            return {"data": data, "mime_type": mime_type}

    class FakeModels:
        @staticmethod
        def generate_content(model, contents):
            captured["model"] = model
            captured["contents"] = contents

            class Response:
                text = '{"scene_state":"WORKING","confidence":0.7,"reason":"desk work"}'

            return Response()

    class FakeClient:
        models = FakeModels()

    classifier = GoogleSceneClassifier(
        client=FakeClient(),
        part_factory=FakePart,
        model="gemma-4-31b-it",
        system_prompt="classify scene",
    )

    result, elapsed = classifier.classify_sync(
        [frame(i) for i in range(6)],
        {"sensors": {"heartRate": 91, "spo2": 97}},
    )

    assert result["scene_state"] == "WORKING"
    assert elapsed >= 0
    assert captured["model"] == "gemma-4-31b-it"
    assert len([item for item in captured["contents"] if isinstance(item, dict) and item["mime_type"] == "image/jpeg"]) == 6
    text_payload = "\n".join(item for item in captured["contents"] if isinstance(item, str))
    assert '"heart_rate_bpm": 91' in text_payload
    assert "spo2" not in text_payload.lower()


def test_resolve_google_api_key_reads_fallback_env_without_exposing_value(tmp_path):
    fallback = tmp_path / ".env"
    fallback.write_text("GOOGLE_API_KEY=fake-from-draft\n", encoding="utf-8")
    settings = make_settings(tmp_path, google_api_key="", scene_awareness_env_fallback_path=fallback)

    assert resolve_google_api_key(settings) == "fake-from-draft"


@pytest.mark.asyncio
async def test_scene_awareness_start_clears_previous_log_and_records_start(tmp_path):
    settings = make_settings(tmp_path)
    settings.scene_awareness_log_path.write_text("old log\n", encoding="utf-8")
    system_state = SystemStateManager(settings)
    service = SceneAwarenessService(settings, FakeCameraService([]), system_state, classifier=FakeClassifier())

    service.start()
    await service.stop()

    log_text = settings.scene_awareness_log_path.read_text(encoding="utf-8")
    assert "old log" not in log_text
    assert '"event":"service_start"' in log_text
    assert '"event":"service_stop"' in log_text


@pytest.mark.asyncio
async def test_scene_awareness_logs_camera_unavailable_and_smart_mode_transitions(tmp_path):
    settings = make_settings(tmp_path)
    system_state = SystemStateManager(settings)
    service = SceneAwarenessService(settings, FakeCameraService([]), system_state, classifier=FakeClassifier())

    await service.collect_once()
    system_state.set_operation_mode(smart_mode_enabled=False, control_mode="manual", scene="relax", source="dashboard")
    await service.collect_once()
    system_state.set_operation_mode(smart_mode_enabled=True, control_mode="scene")
    await service.collect_once()

    log_text = settings.scene_awareness_log_path.read_text(encoding="utf-8")
    assert '"event":"camera_frame_unavailable"' in log_text
    assert '"event":"smart_mode_disabled"' in log_text
    assert '"event":"smart_mode_enabled"' in log_text


@pytest.mark.asyncio
async def test_scene_awareness_collects_buffer_without_inference_when_smart_mode_disabled(tmp_path):
    settings = make_settings(tmp_path)
    system_state = SystemStateManager(settings)
    system_state.set_operation_mode(smart_mode_enabled=False, control_mode="manual", scene="relax", source="dashboard")
    camera = FakeCameraService([frame(i) for i in range(6)])
    classifier = FakeClassifier()
    service = SceneAwarenessService(settings, camera, system_state, classifier=classifier)

    for _ in range(6):
        await service.collect_once()

    assert classifier.calls == []
    assert service.status()["bufferSize"] == 6
    assert system_state.snapshot()["currentContext"]["scene"] == "relax"


@pytest.mark.asyncio
async def test_scene_awareness_inferrs_immediately_after_smart_mode_reenabled_with_full_buffer(tmp_path):
    settings = make_settings(tmp_path)
    system_state = SystemStateManager(settings)
    system_state.set_operation_mode(smart_mode_enabled=False, control_mode="manual", scene="relax", source="dashboard")
    camera = FakeCameraService([frame(i) for i in range(7)])
    classifier = FakeClassifier()
    service = SceneAwarenessService(settings, camera, system_state, classifier=classifier, infer_now=lambda: 31.0)

    for _ in range(6):
        await service.collect_once()
    system_state.set_operation_mode(smart_mode_enabled=True, control_mode="scene", scene="relax", source="activation")
    await service.collect_once()
    await asyncio.sleep(0)

    assert len(classifier.calls) == 1
    assert len(classifier.calls[0][0]) == 6
    assert service.status()["lastResult"]["scene_state"] == "WORKING"


@pytest.mark.asyncio
async def test_scene_awareness_collects_buffer_without_inference_when_context_is_vacant(tmp_path):
    settings = make_settings(tmp_path)
    system_state = SystemStateManager(settings)
    camera = FakeCameraService([frame(i) for i in range(6)])
    classifier = FakeClassifier()
    service = SceneAwarenessService(settings, camera, system_state, classifier=classifier, infer_now=lambda: 31.0)

    for _ in range(6):
        await service.collect_once()

    await asyncio.sleep(0)
    assert classifier.calls == []
    assert service.status()["bufferSize"] == 6
    assert service.status()["requestsStarted"] == 0
    assert system_state.snapshot()["currentContext"]["scene"] == "vacant"
    log_text = settings.scene_awareness_log_path.read_text(encoding="utf-8")
    assert '"event":"inference_paused"' in log_text
    assert '"reason":"context_vacant"' in log_text


@pytest.mark.asyncio
async def test_scene_awareness_records_observation_without_overwriting_context(tmp_path):
    settings = make_settings(tmp_path)
    system_state = SystemStateManager(settings)
    system_state.set_operation_mode(smart_mode_enabled=True, control_mode="scene", scene="relax", source="activation")
    seen = []
    system_state.add_context_listener(lambda snapshot: seen.append(snapshot["currentContext"]["scene"]))
    system_state.apply_mqtt_message("integration/smart/v1/wearable/pico_wearable01/status", '{"status":{"heart_rate":88}}')
    camera = FakeCameraService([frame(i) for i in range(6)])
    classifier = FakeClassifier()
    service = SceneAwarenessService(settings, camera, system_state, classifier=classifier, infer_now=lambda: 31.0)

    for _ in range(6):
        await service.collect_once()

    await asyncio.sleep(0)
    snapshot = system_state.snapshot()
    status = service.status()
    assert snapshot["currentContext"] == {
        "scene": "relax",
        "source": "activation",
        "confidence": None,
        "reason": "",
    }
    assert seen == []
    assert status["lastResult"]["scene_state"] == "WORKING"
    assert status["lastResult"]["mapped_scene"] == "work"
    assert status["lastResult"]["confidence"] == 0.82
    assert classifier.calls[0][1]["sensors"]["heartRate"] == 88
    log_text = settings.scene_awareness_log_path.read_text(encoding="utf-8")
    assert '"event":"request_start"' in log_text
    assert '"event":"response_received"' in log_text
    assert '"scene_state":"WORKING"' in log_text
    assert '"mapped_scene":"work"' in log_text


@pytest.mark.asyncio
async def test_scene_awareness_skips_overlapping_inference(tmp_path):
    settings = make_settings(tmp_path)
    system_state = SystemStateManager(settings)
    system_state.set_operation_mode(smart_mode_enabled=True, control_mode="scene", scene="relax", source="activation")
    camera = FakeCameraService([frame(i) for i in range(12)])
    release = asyncio.Event()
    classifier = FakeClassifier(wait_for=release)
    now_values = iter([31.0, 62.0])
    service = SceneAwarenessService(settings, camera, system_state, classifier=classifier, infer_now=lambda: next(now_values, 62.0))

    for _ in range(6):
        await service.collect_once()
    await asyncio.sleep(0)
    for _ in range(6):
        await service.collect_once()

    status = service.status()
    assert len(classifier.calls) == 1
    assert status["requestsSkipped"] == 1
    release.set()
    await asyncio.sleep(0)


@pytest.mark.asyncio
async def test_scene_awareness_discards_late_result_after_smart_mode_disabled(tmp_path):
    settings = make_settings(tmp_path)
    system_state = SystemStateManager(settings)
    system_state.set_operation_mode(smart_mode_enabled=True, control_mode="scene", scene="relax", source="activation")
    camera = FakeCameraService([frame(i) for i in range(6)])
    release = asyncio.Event()
    classifier = FakeClassifier(wait_for=release)
    service = SceneAwarenessService(settings, camera, system_state, classifier=classifier, infer_now=lambda: 31.0)

    for _ in range(6):
        await service.collect_once()
    await asyncio.sleep(0)
    system_state.set_operation_mode(smart_mode_enabled=False, control_mode="manual", scene="relax", source="dashboard")
    release.set()
    await asyncio.sleep(0)

    snapshot = system_state.snapshot()
    assert snapshot["currentContext"]["scene"] == "relax"
    assert service.status()["requestsDiscarded"] == 1


@pytest.mark.asyncio
async def test_scene_awareness_unknown_result_does_not_overwrite_context(tmp_path):
    settings = make_settings(tmp_path)
    system_state = SystemStateManager(settings)
    system_state.set_operation_mode(smart_mode_enabled=True, control_mode="scene", scene="relax", source="activation")
    camera = FakeCameraService([frame(i) for i in range(6)])
    classifier = FakeClassifier(response={"scene_state": "UNKNOWN", "confidence": 0.2, "reason": "unclear"})
    service = SceneAwarenessService(settings, camera, system_state, classifier=classifier, infer_now=lambda: 31.0)

    for _ in range(6):
        await service.collect_once()
    await asyncio.sleep(0)

    assert system_state.snapshot()["currentContext"]["scene"] == "relax"
    assert service.status()["lastResult"]["scene_state"] == "UNKNOWN"


@pytest.mark.asyncio
async def test_scene_awareness_sends_completed_observation_to_fusion_control(tmp_path):
    settings = make_settings(tmp_path)
    system_state = SystemStateManager(settings)
    system_state.set_operation_mode(smart_mode_enabled=True, control_mode="scene", scene="relax", source="activation")
    camera = FakeCameraService([frame(i) for i in range(6)])
    classifier = FakeClassifier()
    fusion = FakeFusionControl()
    service = SceneAwarenessService(
        settings,
        camera,
        system_state,
        classifier=classifier,
        fusion_control=fusion,
        infer_now=lambda: 31.0,
    )

    for _ in range(6):
        await service.collect_once()
    await asyncio.sleep(0)

    assert fusion.calls == [service.status()["lastResult"]]


def test_scene_awareness_status_endpoint_is_available_without_api_key(tmp_path):
    settings = make_settings(tmp_path, scene_awareness_enabled=True, google_api_key="")
    client = TestClient(create_app(settings=settings, publisher=FakePublisher(), camera_service=FakeCameraService([])))

    response = client.get("/api/scene-awareness/status")

    assert response.status_code == 200
    payload = response.json()
    assert payload["enabled"] is False
    assert payload["reason"] == "missing_google_api_key"


def test_read_recent_log_lines_returns_tail(tmp_path):
    log_path = tmp_path / "scene_awareness.log"
    log_path.write_text("a\nb\nc\n", encoding="utf-8")

    assert read_recent_log_lines(log_path, limit=2) == ["b", "c"]


class FakePublisher:
    def is_connected(self, name):
        return False

    def publish(self, name, topic, payload, qos=1):
        raise AssertionError("scene awareness status should not publish")
