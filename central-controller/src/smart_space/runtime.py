from __future__ import annotations

import os
from collections.abc import Mapping
from typing import Any

import uvicorn


DEFAULT_APP = "smart_space.main:app"
DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 3000


def build_uvicorn_config(env: Mapping[str, str] | None = None) -> dict[str, Any]:
    values = env if env is not None else os.environ
    return {
        "app": values.get("SMART_SPACE_APP", DEFAULT_APP),
        "host": values.get("SMART_SPACE_HOST", DEFAULT_HOST),
        "port": _int_env(values.get("PORT"), DEFAULT_PORT),
        "reload": _truthy(values.get("SMART_SPACE_RELOAD")),
    }


def startup_summary(config: Mapping[str, Any]) -> str:
    return "\n".join([
        "Starting Smart Space runtime",
        f"- app: {config['app']}",
        f"- bind: {config['host']}:{config['port']}",
        "- dashboard service: FastAPI static files and WebSocket API",
        "- global state manager: in-process SystemStateManager",
        "- MQTT bridge: lifecycle-managed broker clients",
        "- music service: local ffplay playback with dashboard/API controls",
        "- scene awareness service: Google AI visual context classification when smart mode is enabled",
        "- fusion control engine: scene state machine and GPIO/PIR activation handling",
    ])


def main() -> None:
    config = build_uvicorn_config()
    print(startup_summary(config), flush=True)
    uvicorn.run(
        config["app"],
        host=config["host"],
        port=config["port"],
        reload=config["reload"],
    )


def _int_env(value: str | None, fallback: int) -> int:
    try:
        return int(value) if value not in (None, "") else fallback
    except ValueError:
        return fallback


def _truthy(value: str | None) -> bool:
    return str(value or "").strip().lower() in {"1", "true", "yes", "on"}


if __name__ == "__main__":
    main()
