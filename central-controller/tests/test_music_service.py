from pathlib import Path

import pytest

from smart_space.core.settings import Settings
from smart_space.services.music import LocalMusicService


class FakePlayerBackend:
    def __init__(self):
        self.started = []
        self.paused = 0
        self.resumed = 0
        self.stopped = 0

    def start(self, path: Path):
        self.started.append(path)
        return object()

    def pause(self, _handle):
        self.paused += 1

    def resume(self, _handle):
        self.resumed += 1

    def stop(self, _handle):
        self.stopped += 1


class FakeAudioController:
    def __init__(self):
        self.volume = 50
        self.output = "default"
        self.volume_calls = []
        self.output_calls = []

    def current_volume(self):
        return self.volume

    def set_volume(self, volume):
        self.volume = volume
        self.volume_calls.append(volume)

    def current_output(self):
        return self.output

    def set_output(self, output):
        self.output = output
        self.output_calls.append(output)

    def list_outputs(self):
        return [{"id": "default", "name": "Default Output"}]


def make_settings(tmp_path):
    return Settings(
        port=0,
        dashboard_dir=Path("src/smart_space/web/dashboard"),
        led_profile_default_path=Path("src/smart_space/config/led_profiles.default.json"),
        led_profile_user_path=tmp_path / "led_profiles.user.json",
        music_dir=tmp_path / "music",
    )


@pytest.mark.asyncio
async def test_scans_supported_scene_folder_and_ignores_other_files(tmp_path):
    music_dir = tmp_path / "music"
    work = music_dir / "work"
    work.mkdir(parents=True)
    (work / "focus.mp3").write_bytes(b"")
    (work / "notes.txt").write_text("ignore", encoding="utf-8")

    service = LocalMusicService(make_settings(tmp_path), player_backend=FakePlayerBackend(), audio_controller=FakeAudioController())

    tracks = service.scan_scene("work")

    assert tracks == [work / "focus.mp3"]


@pytest.mark.asyncio
async def test_vacant_scene_stops_without_creating_or_scanning_directory(tmp_path):
    player = FakePlayerBackend()
    service = LocalMusicService(make_settings(tmp_path), player_backend=player, audio_controller=FakeAudioController())
    await service.handle_command("play", {"trackPath": str(tmp_path / "song.mp3")})

    result = await service.on_context_scene("vacant")

    assert result["reason"] == "vacant"
    assert player.stopped == 1
    assert not (tmp_path / "music" / "vacant").exists()
    assert service.snapshot()["playing"] is False


@pytest.mark.asyncio
async def test_auto_scene_change_randomly_plays_matching_scene(tmp_path):
    relax = tmp_path / "music" / "relax"
    relax.mkdir(parents=True)
    (relax / "a.mp3").write_bytes(b"")
    (relax / "b.ogg").write_bytes(b"")
    player = FakePlayerBackend()
    service = LocalMusicService(make_settings(tmp_path), player_backend=player, audio_controller=FakeAudioController(), chooser=lambda items: items[-1])

    result = await service.on_context_scene("relax")

    assert result["ok"] is True
    assert result["scene"] == "relax"
    assert player.started == [relax / "b.ogg"]
    assert service.snapshot()["track"] == "b.ogg"
    assert service.snapshot()["mode"] == "relax"


@pytest.mark.asyncio
async def test_empty_scene_returns_non_crashing_result(tmp_path):
    service = LocalMusicService(make_settings(tmp_path), player_backend=FakePlayerBackend(), audio_controller=FakeAudioController())

    result = await service.on_context_scene("sleep")

    assert result == {"ok": False, "reason": "empty_scene", "scene": "sleep"}
    assert service.snapshot()["playing"] is False


@pytest.mark.asyncio
async def test_controls_playback_volume_and_output_with_fake_backends(tmp_path):
    song = tmp_path / "song.mp3"
    song.write_bytes(b"")
    player = FakePlayerBackend()
    audio = FakeAudioController()
    service = LocalMusicService(make_settings(tmp_path), player_backend=player, audio_controller=audio)

    await service.handle_command("play", {"trackPath": str(song)})
    await service.handle_command("pause", {})
    await service.handle_command("resume", {})
    await service.handle_command("set_volume", {"volume": 72})
    await service.handle_command("set_output", {"outputDevice": "speaker"})

    assert player.started == [song, song]
    assert player.paused == 1
    assert player.resumed == 1
    assert audio.volume_calls == [72]
    assert audio.output_calls == ["speaker"]
    assert service.snapshot()["volume"] == 72
    assert service.snapshot()["outputDevice"] == "speaker"
