"""Check host pacing without changing fixed-step simulation semantics."""
import json
from pathlib import Path
import subprocess
import sys
import time

binary = str(Path(sys.argv[1]).resolve())
project = str(Path(sys.argv[2]).resolve() / "examples" / "lantern")


def run(*args):
    return subprocess.run([binary, *args], capture_output=True, text=True, timeout=10)


start = time.monotonic()
paced = run("--headless", "--realtime", project, "--frames", "6")
elapsed = time.monotonic() - start
if paced.returncode or not 0.075 <= elapsed < 8:
    raise AssertionError(f"pacing failed: {elapsed:.3f}s; {paced.stderr}")
fast = run("--headless", project, "--frames", "6")
if fast.returncode or json.loads(fast.stdout) != json.loads(paced.stdout):
    raise AssertionError("pacing changed fixed-step world state")
for args in [("--realtime", project), ("--headless", "--check", "--realtime", project)]:
    result = run(*args)
    if not result.returncode or json.loads(result.stderr)["code"] != "arguments":
        raise AssertionError("invalid realtime combination accepted")
print("headless pacing preserves fixed-step state")
