from __future__ import annotations

import json
import shutil
from pathlib import Path
from typing import Any

from smart_space.core.settings import Settings


class LedProfileRepository:
    def __init__(self, settings: Settings):
        self.default_path = settings.led_profile_default_path
        self.user_path = settings.led_profile_user_path

    def ensure_files(self) -> None:
        self.default_path.parent.mkdir(parents=True, exist_ok=True)
        self.user_path.parent.mkdir(parents=True, exist_ok=True)
        if not self.user_path.exists() and self.default_path.exists():
            shutil.copyfile(self.default_path, self.user_path)

    def read(self) -> dict[str, Any]:
        self.ensure_files()
        defaults = self._read_json(self.default_path, {"version": 1, "scene_profiles": {}, "custom_profiles": {}})
        user = self._read_json(self.user_path, defaults)
        return normalize_led_profiles({
            "version": user.get("version") or defaults.get("version") or 1,
            "scene_profiles": {**defaults.get("scene_profiles", {}), **user.get("scene_profiles", {})},
            "custom_profiles": {**defaults.get("custom_profiles", {}), **user.get("custom_profiles", {})},
        })

    def update(self, section: str, key: str, patch: dict[str, Any]) -> dict[str, Any]:
        profiles = self.read()
        if section not in {"scene_profiles", "custom_profiles"}:
            raise ValueError("invalid_profile_section")
        if key not in profiles.get(section, {}):
            raise ValueError("invalid_profile_key")
        next_profile = {**profiles[section][key], **(patch or {})}
        profiles[section][key] = (
            normalize_custom_profile(key, next_profile)
            if section == "custom_profiles"
            else profile_with_range(next_profile)
        )
        normalized = normalize_led_profiles(profiles)
        self._write_user(normalized)
        return normalized

    def reset(self, section: str, key: str) -> dict[str, Any]:
        defaults = self._read_json(self.default_path, {"version": 1, "scene_profiles": {}, "custom_profiles": {}})
        profiles = self.read()
        if section not in {"scene_profiles", "custom_profiles"}:
            raise ValueError("invalid_profile_section")
        if key not in defaults.get(section, {}):
            raise ValueError("invalid_profile_key")
        profiles[section][key] = (
            normalize_custom_profile(key, defaults[section][key])
            if section == "custom_profiles"
            else profile_with_range(defaults[section][key])
        )
        normalized = normalize_led_profiles(profiles)
        self._write_user(normalized)
        return normalized

    def _write_user(self, profiles: dict[str, Any]) -> None:
        self.user_path.parent.mkdir(parents=True, exist_ok=True)
        self.user_path.write_text(json.dumps(profiles, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    @staticmethod
    def _read_json(path: Path, fallback: dict[str, Any]) -> dict[str, Any]:
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except (FileNotFoundError, json.JSONDecodeError):
            return fallback


def profile_with_range(profile: dict[str, Any]) -> dict[str, Any]:
    next_profile = dict(profile)
    try:
        target = float(next_profile["target_lux"])
        tolerance = float(next_profile["tolerance_lux"])
    except (KeyError, TypeError, ValueError):
        return next_profile
    next_profile["target_lux_min"] = max(0, round(target - tolerance))
    next_profile["target_lux_max"] = round(target + tolerance)
    return next_profile


def normalize_custom_profile(key: str, profile: dict[str, Any] | None = None) -> dict[str, Any]:
    profile = profile or {}
    fixed = key == "fixed_brightness"
    base = {
        "label": "固定亮度" if fixed else "目標照度範圍",
        "custom_control_type": "fixed_brightness" if fixed else "target_lux_range",
        "fixed_brightness_pct": 64 if fixed else None,
        "target_lux": None if fixed else 500,
        "tolerance_lux": None if fixed else 75,
        "target_lux_min": None if fixed else 425,
        "target_lux_max": None if fixed else 575,
        "min_output": 0,
        "max_output": 100,
        "color_mode": "cct",
        "color_temp_k": 3900 if fixed else 4000,
        "rgb": {"r": 255, "g": 160, "b": 80},
        "effect": "static",
        "flow_preset": "soft_rainbow",
        "flow_speed": "medium",
        "flow_brightness": 64,
        "breathing_speed": "slow",
        "breathing_strength": 50,
    }
    if not fixed:
        base["adaptive_profile"] = "normal"
    next_profile = profile_with_range({**base, **profile})
    if not isinstance(next_profile.get("rgb"), dict):
        next_profile["rgb"] = dict(base["rgb"])
    return next_profile


def normalize_led_profiles(profiles: dict[str, Any] | None = None) -> dict[str, Any]:
    profiles = profiles or {}
    next_profiles = {
        "version": profiles.get("version") or 1,
        "scene_profiles": dict(profiles.get("scene_profiles") or {}),
        "custom_profiles": dict(profiles.get("custom_profiles") or {}),
    }
    for key in ("fixed_brightness", "target_lux_range"):
        next_profiles["custom_profiles"][key] = normalize_custom_profile(
            key,
            next_profiles["custom_profiles"].get(key),
        )
    return next_profiles
