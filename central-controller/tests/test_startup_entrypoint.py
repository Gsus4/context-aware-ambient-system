import re
from pathlib import Path

from smart_space.runtime import build_uvicorn_config, startup_summary


def test_runtime_builds_default_uvicorn_config():
    config = build_uvicorn_config({})

    assert config == {
        "app": "smart_space.main:app",
        "host": "0.0.0.0",
        "port": 3000,
        "reload": False,
    }


def test_runtime_builds_config_from_environment():
    config = build_uvicorn_config({
        "SMART_SPACE_APP": "custom.module:app",
        "SMART_SPACE_HOST": "127.0.0.1",
        "PORT": "8080",
        "SMART_SPACE_RELOAD": "true",
    })

    assert config == {
        "app": "custom.module:app",
        "host": "127.0.0.1",
        "port": 8080,
        "reload": True,
    }


def test_startup_summary_names_core_runtime_services():
    summary = startup_summary(build_uvicorn_config({}))

    assert "dashboard service" in summary
    assert "global state manager" in summary
    assert "MQTT bridge" in summary
    assert "music service" in summary
    assert "fusion control engine" in summary


def test_start_smart_space_shell_script_is_project_entrypoint():
    script = Path("START_SMART_SPACE.sh")

    assert script.exists()
    source = script.read_text(encoding="utf-8")
    assert "uv run python -m smart_space.runtime" in source
    assert "DASHBOARD_DIR" in source
    assert "PORT" in source


def test_start_smart_space_shell_script_reports_port_pid_without_killing():
    source = Path("START_SMART_SPACE.sh").read_text(encoding="utf-8")

    assert "check_port_available" in source
    assert "lsof" in source
    assert "[ERROR] Port ${PORT} is already in use by PID(s):" in source
    assert not re.search(r"(^|[;&|()\s])kill(\s|-)", source)
