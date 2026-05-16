from pathlib import Path

from smart_space.scene_awareness_monitor import read_log_snapshot


def test_read_log_snapshot_returns_recent_log_lines(tmp_path):
    log_path = tmp_path / "scene_awareness.log"
    log_path.write_text("first\nsecond\nthird\n", encoding="utf-8")

    assert read_log_snapshot(log_path, limit=2) == ["second", "third"]


def test_read_log_snapshot_reports_missing_log(tmp_path):
    missing = tmp_path / "missing.log"

    assert read_log_snapshot(missing, limit=20) == [f"log_missing path={missing}"]
