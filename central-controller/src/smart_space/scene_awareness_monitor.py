from __future__ import annotations

import argparse
import time
from pathlib import Path

from smart_space.services.scene_awareness import read_recent_log_lines


DEFAULT_LOG_PATH = Path(".runtime/scene_awareness.log")
DEFAULT_INTERVAL_SECONDS = 1.0


def read_log_snapshot(path: str | Path, *, limit: int = 80) -> list[str]:
    log_path = Path(path)
    if not log_path.exists():
        return [f"log_missing path={log_path}"]
    return read_recent_log_lines(log_path, limit=limit)


def follow_log(path: str | Path, *, interval_seconds: float = DEFAULT_INTERVAL_SECONDS, from_start: bool = False) -> None:
    log_path = Path(path)
    position = 0 if from_start else _file_size(log_path)
    while True:
        if not log_path.exists():
            print(f"log_missing path={log_path}", flush=True)
            time.sleep(max(0.2, interval_seconds))
            position = 0
            continue
        size = log_path.stat().st_size
        if size < position:
            position = 0
        with log_path.open("r", encoding="utf-8") as handle:
            handle.seek(position)
            for line in handle:
                print(line.rstrip("\n"), flush=True)
            position = handle.tell()
        time.sleep(max(0.2, interval_seconds))


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Tail Smart Space scene awareness runtime log.")
    parser.add_argument("--log-path", type=Path, default=DEFAULT_LOG_PATH)
    parser.add_argument("--interval", type=float, default=DEFAULT_INTERVAL_SECONDS, help="File polling interval in seconds.")
    parser.add_argument("--lines", type=int, default=80, help="Number of existing log lines to print with --once.")
    parser.add_argument("--from-start", action="store_true", help="Follow from the start of the current log file.")
    parser.add_argument("--once", action="store_true", help="Print recent log lines and exit.")
    return parser


def main() -> None:
    args = build_arg_parser().parse_args()
    if args.once:
        for line in read_log_snapshot(args.log_path, limit=args.lines):
            print(line, flush=True)
        return
    for line in read_log_snapshot(args.log_path, limit=args.lines):
        print(line, flush=True)
    follow_log(args.log_path, interval_seconds=args.interval, from_start=args.from_start)


def _file_size(path: Path) -> int:
    try:
        return path.stat().st_size
    except FileNotFoundError:
        return 0


if __name__ == "__main__":
    main()
