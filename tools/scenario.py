#!/usr/bin/env python3
"""Run a game replay and assert explicit snapshot paths from a JSON manifest."""
import argparse
import json
import math
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    base = args.manifest.resolve().parent
    command = [str(args.binary.resolve()), "--headless", str((base / manifest.get("project", ".")).resolve()),
               "--frames", str(manifest["frames"]), "--seed", str(manifest.get("seed", 42))]
    if "replay" in manifest:
        command += ["--replay", str(base / manifest["replay"])]
    run = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=manifest.get("timeout", 60))
    if run.returncode:
        raise RuntimeError(run.stderr)
    snapshot = json.loads(run.stdout)
    for check in manifest.get("assertions", []):
        value = snapshot
        for segment in check["path"].split("."):
            value = value[int(segment)] if isinstance(value, list) else value[segment]
        if "equals" in check and value != check["equals"]:
            raise AssertionError(f"{check['path']}: expected {check['equals']!r}, got {value!r}")
        if "near" in check and not math.isclose(value, check["near"], rel_tol=0, abs_tol=check.get("tolerance", 1e-5)):
            raise AssertionError(f"{check['path']}: {value!r} differs from {check['near']!r}")
        if "min" in check and value < check["min"]:
            raise AssertionError(f"{check['path']}: {value!r} below {check['min']!r}")
    print(json.dumps({"ok": True, "assertions": len(manifest.get("assertions", []))}))


if __name__ == "__main__":
    main()
