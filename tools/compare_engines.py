#!/usr/bin/env python3
"""Compare two built engines against one unchanged Lua project and replay."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def inspect(binary: Path, project: Path, replay: Path, frames: int) -> tuple[dict, dict]:
    def run(*args: str) -> dict:
        result = subprocess.run([str(binary), *args], capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise RuntimeError(f"{binary.name}: {result.stderr.strip() or result.stdout.strip()}")
        value = json.loads(result.stdout)
        if not isinstance(value, dict):
            raise ValueError(f"{binary.name}: expected a JSON object")
        return value

    api = run("--api")
    arguments = ("--headless", str(project), "--frames", str(frames), "--seed", "42", "--replay", str(replay))
    state = run(*arguments)
    if state.get("ok") is not True:
        raise ValueError(f"{binary.name}: headless run did not report success")
    if run(*arguments) != state:
        raise RuntimeError(f"{binary.name}: repeated seed/replay produced different state")
    info = {"binary": str(binary), "bytes": binary.stat().st_size,
            "size_kind": "input file bytes; no stripping performed", "repeatable": True,
            "version": api["version"], "language": api.get("language"),
            "network": api.get("network", {"available": False}),
            "functions": sorted(item["name"] for item in api["functions"]), "state_hash": state["hash"]}
    # The engine version is build metadata, not simulated game behavior.
    state.pop("version", None)
    if "language" in state:
        info["snapshot_language"] = state.pop("language")
    return info, state


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--project", type=Path, default=ROOT / "examples/lantern")
    parser.add_argument("--replay", type=Path, default=ROOT / "examples/lantern/replays/tour.txt")
    parser.add_argument("--frames", type=int, default=480)
    parser.add_argument("--output", type=Path, help="also save the JSON report at this path")
    args = parser.parse_args()
    if not 0 <= args.frames <= 1_000_000_000:
        parser.error("--frames must be in 0..1000000000")
    try:
        baseline, old = inspect(args.baseline.resolve(), args.project.resolve(), args.replay.resolve(), args.frames)
        candidate, new = inspect(args.candidate.resolve(), args.project.resolve(), args.replay.resolve(), args.frames)
        changed = sorted(key for key in old.keys() | new.keys() if old.get(key) != new.get(key))
        report = {"baseline": baseline, "candidate": candidate, "frames": args.frames, "seed": 42,
                  "size_delta_bytes": candidate["bytes"] - baseline["bytes"],
                  "same_game_state": not changed, "changed_state_fields": changed,
                  "note": "Size comparisons require matching graphics/network, Release/Debug and stripping options; this is not a timing benchmark."}
        output = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            args.output.write_text(output, encoding="utf-8")
        print(output, end="")
        return int(bool(changed))
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as error:
        parser.exit(2, f"comparison failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
