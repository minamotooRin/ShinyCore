"""Short shipped replay for Crossing's analog movement and disconnect release."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parents[1]
project = root / "examples/crossing"

with tempfile.TemporaryDirectory(prefix="shiny-crossing-analog-") as directory:
    trace = Path(directory) / "analog.trace"
    result = subprocess.run(
        [str(binary), str(project), "--headless", "--frames", "100",
         "--replay", str(project / "analog.jsonl"), "--trace", str(trace),
         "--save-dir", directory, "--mute"],
        capture_output=True, text=True, encoding="utf-8", timeout=20,
    )
    assert result.returncode == 0, result.stderr
    final = json.loads(result.stdout)
    assert final["watches"]["crossing"]["mode"] == "game"
    assert final["watches"]["crossing"]["deaths"] == 0
    frames = {frame["tick"]: frame for frame in map(json.loads, trace.read_text(encoding="utf-8").splitlines())}

    def player(tick):
        return next(entity for entity in frames[tick]["entities"] if entity["tag"] == "player")

    # Native 0.2 deadzone remaps 0.6 stick displacement to half of full speed.
    assert abs(player(30)["vx"] - 45) < .01
    assert abs(player(61)["vx"] - 45) < .01
    assert abs(player(64)["vx"]) < .01
    assert abs(player(66)["vx"] + 45) < .01
    assert abs(player(80)["vx"] + 45) < .01
    assert not frames[81]["input"]["gamepad"]["connected"]
    assert abs(player(81)["vx"]) < .01
    assert abs(player(81)["x"] - player(90)["x"]) < .01
    assert frames[100]["input"]["gamepad"]["connected"]
    assert abs(player(100)["vx"]) < .01

print("Crossing analog replay: partial speed, neutral, reversal and disconnect passed")
