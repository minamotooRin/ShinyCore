"""The Wayfarer ending is published only after its checkpoint is durable."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


root = Path(__file__).resolve().parents[1]
source = root / "examples/wayfarer"
binary = Path(sys.argv[1]).resolve()

with tempfile.TemporaryDirectory(prefix="shiny-wayfarer-ending-") as directory:
    temporary = Path(directory)
    project = temporary / "game"
    shutil.copytree(source, project)
    quest = project / "quest.lua"
    script = quest.read_text(encoding="utf-8")
    assert script.endswith("return Quest\n")
    # Reach the real ending save path on the first conversation, without walking
    # the entire map before deliberately blocking the checkpoint's temp file.
    quest.write_text(script[:-len("return Quest\n")] + """
local talk=Quest.talk
function Quest.talk(stage,herbs,cleared,use)
    if stage=="meet" then return "complete","交付成功。",true end
    return talk(stage,herbs,cleared,use)
end
return Quest
""", encoding="utf-8")
    saves = temporary / "saves"
    blocker = saves / "shiny.wayfarer/checkpoint.json.tmp"
    blocker.mkdir(parents=True)
    trace = temporary / "trace.jsonl"
    result = subprocess.run([str(binary), str(project), "--headless", "--frames", "35",
                             "--replay", str(project / "journal.jsonl"), "--save-dir", str(saves),
                             "--trace", str(trace)],
                            capture_output=True, text=True, encoding="utf-8", timeout=30)
    assert result.returncode != 0 and '"code":"save"' in result.stderr, result.stderr
    snapshots = [json.loads(line) for line in trace.read_text(encoding="utf-8").splitlines()]
    snapshot = snapshots[-1]
    assert snapshot["watches"]["save"]["kind"] == "save", snapshot["watches"]
    assert all(frame["watches"]["quest"].get("complete") is not True for frame in snapshots)
    assert all(frame["state"].get("quest_stage") != "complete" for frame in snapshots)
    assert snapshot["watches"]["quest"]["stage"] == "meet", snapshot["watches"]
    assert snapshot["watches"]["quest"]["complete"] is False
    assert snapshot["watches"]["quest"]["mode"] != "end"
    assert snapshot["state"]["quest_stage"] == "meet", snapshot["state"]
    assert not (blocker.parent / "checkpoint.json").exists()
    if len(sys.argv) > 2:
        capture = Path(sys.argv[2]).resolve()
        capture.parent.mkdir(parents=True, exist_ok=True)
        main = project / "main.lua"
        (project / "game.lua").write_text(main.read_text(encoding="utf-8"), encoding="utf-8")
        main.write_text("""local World=require('shiny.stream_world')
local update=World.ui_update
World.ui_update=function(world)
    update(world)
    local status=World.status(world)
    if status and status.status=='failed' then sc.app.quit() end
end
return require('game')
""", encoding="utf-8")
        native = subprocess.run([str(binary), str(project), "--frames", "35", "--mute",
                                 "--capture-hidden", "--capture", str(capture),
                                 "--replay", str(project / "journal.jsonl"), "--save-dir", str(saves)],
                                capture_output=True, text=True, encoding="utf-8", timeout=30)
        assert native.returncode != 0 and '"code":"save"' in native.stderr, native.stderr
        assert capture.is_file(), native.stderr

print("Wayfarer ending: failed checkpoint does not publish completion")
