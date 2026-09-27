#!/usr/bin/env python3
"""Run a replay and check final, exact-frame and any-frame snapshots."""

import argparse
import json
import math
import subprocess
import tempfile
from pathlib import Path


class ScenarioError(Exception):
    pass


class MissingPath(ScenarioError):
    pass


def frame_number(value, limit, label):
    if type(value) is not int or not 1 <= value <= limit:
        raise ScenarioError(f"{label} must be an integer in 1..{limit}")
    return value


def select(snapshot, path):
    value = snapshot
    for segment in path.split("."):
        try:
            if isinstance(value, list):
                index = int(segment)
                if index < 0:
                    raise IndexError(index)
                value = value[index]
            else:
                value = value[segment]
        except (KeyError, IndexError, ValueError, TypeError) as error:
            raise MissingPath(f"{path}: missing segment {segment!r}") from error
    return value


def matches(snapshot, check):
    path = check["path"]
    value = select(snapshot, path)
    if "equals" in check and (type(value) is bool) != (type(check["equals"]) is bool):
        return False, f"expected {check['equals']!r}, got {value!r}"
    if "equals" in check and value != check["equals"]:
        return False, f"expected {check['equals']!r}, got {value!r}"

    for field in ("near", "min", "max"):
        if field not in check:
            continue
        bound = check[field]
        if (type(value) not in (int, float) or type(bound) not in (int, float)
                or not math.isfinite(value) or not math.isfinite(bound)):
            raise ScenarioError(f"{path}: {field} requires finite numbers")
        if field == "near":
            tolerance = check.get("tolerance", 1e-5)
            if (type(tolerance) not in (int, float) or not math.isfinite(tolerance)
                    or tolerance < 0):
                raise ScenarioError(f"{path}: tolerance must be finite and nonnegative")
            if not math.isclose(value, bound, rel_tol=0, abs_tol=tolerance):
                return False, f"{value!r} differs from {bound!r} by more than {tolerance!r}"
        elif field == "min" and value < bound:
            return False, f"{value!r} below {bound!r}"
        elif field == "max" and value > bound:
            return False, f"{value!r} above {bound!r}"

    if "contains" in check:
        if type(value) not in (list, dict, str):
            raise ScenarioError(f"{path}: contains requires a list, object or string")
        if check["contains"] not in value:
            return False, f"{value!r} does not contain {check['contains']!r}"
    if "length" in check:
        length = check["length"]
        if type(value) not in (list, dict, str) or type(length) is not int or length < 0:
            raise ScenarioError(f"{path}: length requires a nonnegative integer and a container")
        if len(value) != length:
            return False, f"length {len(value)} differs from {length}"
    return True, ""


def run(binary, manifest_path):
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if type(manifest) is not dict:
        raise ScenarioError("manifest must be an object")
    frames = manifest.get("frames")
    if type(frames) is not int or frames < 1:
        raise ScenarioError("frames must be a positive integer")
    assertions = manifest.get("assertions", [])
    if type(assertions) is not list:
        raise ScenarioError("assertions must be an array")

    exact, windows = {}, []
    for check in assertions:
        if type(check) is not dict or type(check.get("path")) is not str or not check["path"]:
            raise ScenarioError("each assertion requires a nonempty path")
        if "frame" in check and "any_frame" in check:
            raise ScenarioError(f"{check['path']}: choose frame or any_frame")
        if "frame" in check:
            exact.setdefault(frame_number(check["frame"], frames, "frame"), []).append(check)
        elif "any_frame" in check:
            window = check["any_frame"]
            if type(window) is not dict:
                raise ScenarioError(f"{check['path']}: any_frame requires start/end")
            start = frame_number(window.get("start", 1), frames, "start")
            end = frame_number(window.get("end", frames), frames, "end")
            if end < start:
                raise ScenarioError(f"{check['path']}: end precedes start")
            windows.append((start, end, check))

    base = manifest_path.resolve().parent
    command = [str(binary.resolve()), "--headless", str((base / manifest.get("project", ".")).resolve()),
               "--frames", str(frames), "--seed", str(manifest.get("seed", 42))]
    if "replay" in manifest:
        command += ["--replay", str((base / manifest["replay"]).resolve())]
    with tempfile.TemporaryDirectory(prefix="shiny-scenario-") as directory:
        command += ["--save-dir", directory]
        trace = Path(directory) / "trace.jsonl"
        if exact or windows:
            command += ["--trace", str(trace)]
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                                timeout=manifest.get("timeout", 60))
        if result.returncode:
            raise ScenarioError(result.stderr.strip() or f"engine exited {result.returncode}")
        final = json.loads(result.stdout)
        matched = [False] * len(windows)
        seen = set()
        if exact or windows:
            with trace.open(encoding="utf-8") as lines:
                for line in lines:
                    snapshot = json.loads(line)
                    frame = snapshot["frames"]
                    if frame in exact:
                        seen.add(frame)
                        for check in exact[frame]:
                            ok, why = matches(snapshot, check)
                            if not ok:
                                raise ScenarioError(f"frame {frame} {check['path']}: {why}")
                    for i, (start, end, check) in enumerate(windows):
                        if matched[i] or not start <= frame <= end:
                            continue
                        try:
                            matched[i] = matches(snapshot, check)[0]
                        except MissingPath:
                            pass  # A watch may first appear later in the window.
        for frame in exact:
            if frame not in seen:
                raise ScenarioError(f"frame {frame} was not recorded")
        for i, (start, end, check) in enumerate(windows):
            if not matched[i]:
                raise ScenarioError(f"frames {start}..{end} {check['path']}: no matching frame")

    for check in assertions:
        if "frame" not in check and "any_frame" not in check:
            ok, why = matches(final, check)
            if not ok:
                raise ScenarioError(f"final {check['path']}: {why}")
    return {"ok": True, "frames": final["frames"], "assertions": len(assertions)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    try:
        result = run(args.binary, args.manifest)
    except (ScenarioError, ValueError, KeyError, TypeError, OSError, subprocess.TimeoutExpired) as error:
        print(json.dumps({"ok": False, "error": str(error)}, ensure_ascii=False))
        return 1
    print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
