from __future__ import annotations

from datetime import datetime
from html import escape
from typing import Any


def safe_int(value: Any, min_value: int, max_value: int, fallback: int | None = None) -> int:
    fallback_value = min_value if fallback is None else fallback
    try:
        number = float(value)
    except (TypeError, ValueError):
        return fallback_value
    if number != number:
        return fallback_value
    return round(max(min_value, min(max_value, number)))


def get_field(data: dict[str, Any] | None, names: list[str]) -> Any:
    if not isinstance(data, dict):
        return None
    for name in names:
        value = data.get(name)
        if value not in (None, ""):
            return value
    return None


def get_field_including_null(data: dict[str, Any] | None, names: list[str]) -> Any:
    if not isinstance(data, dict):
        return None
    for name in names:
        if name in data and data[name] != "":
            return data[name]
    return None


def as_number_or_none(value: Any) -> float | int | None:
    if value in (None, ""):
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    if number != number:
        return None
    return int(number) if number.is_integer() else number


def truthy_state(value: Any) -> bool | None:
    if value is True or value == 1:
        return True
    if value is False or value == 0:
        return False
    if isinstance(value, str):
        normalized = value.strip().lower()
        if normalized in {"online", "connected", "on", "true", "yes", "active", "detected", "motion"}:
            return True
        if normalized in {"offline", "disconnected", "off", "false", "no", "inactive", "none"}:
            return False
    return None


def payload_body(data: dict[str, Any] | None) -> dict[str, Any]:
    if not isinstance(data, dict):
        return {}
    for key in ("status", "event", "availability"):
        if isinstance(data.get(key), dict):
            return data[key]
    return data


def normalize_online_from_payload(raw: str, data: dict[str, Any] | None = None) -> bool:
    if isinstance(data, dict):
        container = data.get("availability") if isinstance(data.get("availability"), dict) else data
        value = get_field(container, ["online", "connected", "available", "status"])
        state = truthy_state(value)
        if state is not None:
            return state
    state = truthy_state((raw or "").strip())
    return False if state is None else state


def normalize_hex(hex_value: Any) -> str | None:
    if not isinstance(hex_value, str):
        return None
    value = hex_value.strip()
    if not value.startswith("#"):
        value = f"#{value}"
    if len(value) == 4 and all(ch in "0123456789abcdefABCDEF" for ch in value[1:]):
        value = f"#{value[1]}{value[1]}{value[2]}{value[2]}{value[3]}{value[3]}"
    if len(value) == 7 and all(ch in "0123456789abcdefABCDEF" for ch in value[1:]):
        return value.lower()
    return None


def hex_to_rgb(hex_value: Any) -> dict[str, int] | None:
    normalized = normalize_hex(hex_value)
    if not normalized:
        return None
    return {
        "r": int(normalized[1:3], 16),
        "g": int(normalized[3:5], 16),
        "b": int(normalized[5:7], 16),
    }


def kelvin_to_hex(kelvin: Any) -> str:
    value = safe_int(kelvin, 2700, 6500, 3500)
    if value <= 2900:
        return "#ff9463"
    if value <= 3400:
        return "#ffb75c"
    if value <= 4000:
        return "#ffd89a"
    if value <= 4700:
        return "#fff2d0"
    if value <= 5600:
        return "#eef7ff"
    return "#d8ecff"


def html_escape(value: Any) -> str:
    return escape(str(value), quote=True)


def to_unix_ms(value: Any) -> int | None:
    if isinstance(value, (int, float)):
        return round(value)
    if isinstance(value, str) and value.strip():
        try:
            return round(datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp() * 1000)
        except ValueError:
            return None
    return None


def normalize_timestamp_fields(payload: dict[str, Any]) -> dict[str, Any]:
    if "ts" not in payload:
        return payload
    unix_ms = to_unix_ms(payload.get("ts"))
    if unix_ms is not None:
        if isinstance(payload.get("ts"), str) and "ts_iso" not in payload:
            payload["ts_iso"] = payload["ts"]
        payload["ts"] = unix_ms
    return payload
