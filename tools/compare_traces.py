#!/usr/bin/env python3
"""Compare explicit trace fields and report the first divergent frame/path."""
import argparse
import json
import math
from itertools import zip_longest
from pathlib import Path


def difference(a, b, tolerance=1e-5, path="$"):
    if isinstance(a, bool) or isinstance(b, bool):
        return None if type(a) is type(b) and a == b else (path, a, b)
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return None if math.isclose(a, b, rel_tol=0, abs_tol=tolerance) else (path, a, b)
    if type(a) is not type(b):
        return path, a, b
    if isinstance(a, dict):
        for key in sorted(a.keys() | b.keys()):
            if path=="$" and key in {"hash", "state_hash", "version"}:
                continue
            if key not in a or key not in b:
                return f"{path}.{key}", a.get(key), b.get(key)
            result = difference(a[key], b[key], tolerance, f"{path}.{key}")
            if result:
                return result
    elif isinstance(a, list):
        if len(a) != len(b):
            return path + ".length", len(a), len(b)
        for index, (left, right) in enumerate(zip(a, b)):
            result = difference(left, right, tolerance, f"{path}[{index}]")
            if result:
                return result
    elif a != b:
        return path, a, b
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("--tolerance", type=float, default=1e-5)
    args = parser.parse_args()
    if args.tolerance < 0 or not math.isfinite(args.tolerance):
        parser.error("tolerance must be finite and nonnegative")
    with args.left.open(encoding="utf-8") as left, args.right.open(encoding="utf-8") as right:
        for line, pair in enumerate(zip_longest(left, right), 1):
            if None in pair:
                print(json.dumps({"ok": False, "line": line, "error": "trace lengths differ"}))
                return 1
            a, b = map(json.loads, pair)
            result = difference(a, b, args.tolerance)
            if result:
                path, expected, actual = result
                print(json.dumps({"ok": False, "frame": a.get("frames"), "path": path,
                                  "expected": expected, "actual": actual}, ensure_ascii=False))
                return 1
    print(json.dumps({"ok": True}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
