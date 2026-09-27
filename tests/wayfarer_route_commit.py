"""Keep Wayfarer's quest flag behind an asynchronous road-tile publication."""

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "examples/wayfarer"
PATCH = "local count,phase=World.patch(world,{{x=41,y=16,layer=0,gid=4}})"
PENDING = 'if phase=="pending" then route_pending=true else clear_route() end'


def check(binary, folder, cancel):
    project = folder / ("cancel" if cancel else "publish")
    shutil.copytree(SOURCE, project, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    main = project / "main.lua"
    script = main.read_text(encoding="utf-8")
    assert script.count(PATCH) == 1 and script.count(PENDING) == 1
    script = script.replace(PATCH, "world.resident_names=nil\n                " + PATCH)
    if cancel:
        script = script.replace(PENDING, PENDING.replace("route_pending=true", "route_pending=true; World.cancel(world)"))
    main.write_text(script, encoding="utf-8")

    trace = folder / (project.name + ".jsonl")
    result = subprocess.run([str(binary), "--headless", str(project), "--frames", "126",
                             "--replay", str(SOURCE / "walkthrough.jsonl"), "--trace", str(trace),
                             "--save-dir", str(folder / (project.name + "-saves"))],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    frames = {entry["frames"]: entry for entry in map(json.loads, trace.read_text(encoding="utf-8").splitlines())}
    assert frames[118]["watches"]["save"]["kind"] == "patch"
    assert frames[118]["state"].get("route_cleared") is not True
    assert (frames[126]["state"].get("route_cleared") is True) != cancel


if __name__ == "__main__":
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="shiny-wayfarer-road-") as directory:
        check(binary, Path(directory), False)
        check(binary, Path(directory), True)
    print("Wayfarer road: async publish and cancellation preserve quest state")
