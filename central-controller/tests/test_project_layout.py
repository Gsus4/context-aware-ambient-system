from pathlib import Path

from fastapi.testclient import TestClient

from smart_space.api.app import create_app
from smart_space.core.settings import Settings


class FakePublisher:
    def is_connected(self, name):
        return False

    def publish(self, name, topic, payload, qos=1):
        raise AssertionError("layout tests should not publish MQTT")


def test_default_paths_use_src_owned_dashboard_assets():
    settings = Settings()

    assert settings.dashboard_dir == Path("src/smart_space/web/dashboard")
    assert settings.led_profile_default_path == Path("src/smart_space/config/led_profiles.default.json")
    assert settings.led_profile_user_path == Path("src/smart_space/data/led_profiles.user.json")
    assert not Path("dashboard").exists()
    assert Path("dashboard_old").exists()


def test_fastapi_serves_dashboard_from_src_web_directory():
    client = TestClient(create_app(settings=Settings(), publisher=FakePublisher()))

    html = client.get("/")
    config_js = client.get("/js/config.js")

    assert html.status_code == 200
    assert "智慧情境感知與空間氛圍控制系統" in html.text
    assert config_js.status_code == 200
    assert "FastAPI Backend" in config_js.text


def test_mediamtx_low_latency_hls_keeps_required_segment_count():
    config = Path("mediamtx/mediamtx.yml").read_text(encoding="utf-8")

    assert "hlsVariant: lowLatency" in config
    assert "hlsAlwaysRemux: true" in config
    assert _top_level_config_value(config, "hlsSegmentCount") >= 7


def _top_level_config_value(config: str, key: str) -> int:
    prefix = f"{key}:"
    for line in config.splitlines():
        if line.startswith(prefix):
            return int(line.split(":", 1)[1].strip())
    raise AssertionError(f"{key} is missing")
