"""Capture Wayfarer's real read/write-error UI and saved menu, without desktop focus."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();binary=args.binary.resolve();output=args.output.resolve()
output.mkdir(parents=True,exist_ok=False)
source=Path(__file__).resolve().parents[1]/'examples/wayfarer'
records=[]
with tempfile.TemporaryDirectory(prefix='shiny-wayfarer-save-') as directory:
    temporary=Path(directory);project=temporary/'game'
    shutil.copytree(source,project)
    original=(project/'main.lua').read_text(encoding='utf-8')
    (project/'game.lua').write_text(original,encoding='utf-8')
    # Only terminate after the actual room has observed a real disk failure.
    # Rendering/layout remain the unmodified sample's own callbacks.
    recovery='''local World=require('shiny.stream_world')
local ui_update=World.ui_update
World.ui_update=function(world)
    ui_update(world)
    local status=World.status(world)
    if status and status.status=='failed' then sc.app.quit() end
end
return require('game')
'''
    (project/'main.lua').write_text(recovery,encoding='utf-8')
    saves=temporary/'saves'
    blocker=saves/'shiny.wayfarer/checkpoint.json.tmp';blocker.mkdir(parents=True)
    def capture(name,replay,failed):
        command=[str(binary),str(project),'--frames','45','--replay',str(replay),
                 '--save-dir',str(saves),'--mute','--capture-hidden','--capture',str(output/f'{name}.png')]
        result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=30)
        (output/f'{name}.log').write_text(result.stderr,encoding='utf-8')
        assert (result.returncode!=0)==failed,result.stderr
        assert (output/f'{name}.png').is_file(),result.stderr
        assert 'missing text glyph' not in result.stderr,result.stderr
        if failed: assert '"code":"save"' in result.stderr,result.stderr
        else:
            snapshot=json.loads(result.stdout)
            assert snapshot['state']['traveler']=='小林' and snapshot['state']['equipment']=='field'
            assert snapshot['watches']['save']['status']=='idle'
            (output/f'{name}.json').write_text(result.stdout,encoding='utf-8')
        records.append(dict(case=name,command=command,exit_code=result.returncode,
            capture=f'{name}.png',visual_review='pending',failure_exit_hook=failed))
    capture('save-error',project/'journal.jsonl',True)
    blocker.rmdir()
    # Use a corrupt source slot without changing the game's own recovery UI.
    (saves/'shiny.wayfarer/read-error.json').write_text('{broken',encoding='utf-8')
    redirect="""local create=World.new
World.new=function(options) options.slot='read-error';return create(options) end
"""
    (project/'main.lua').write_text(recovery.replace("local ui_update=World.ui_update",redirect+"local ui_update=World.ui_update"),encoding='utf-8')
    capture('read-error',project/'journal.jsonl',True)
    (project/'main.lua').write_text(original,encoding='utf-8')
    replay=output/'save-menu.jsonl'
    replay.write_text((source/'journal.jsonl').read_text(encoding='utf-8')+
        '{"frame":41,"keys":["escape"],"gamepad":{"connected":false}}\n'
        '{"frame":42,"keys":[],"gamepad":{"connected":false}}\n',encoding='utf-8')
    capture('save-menu',replay,False)
    manifest=dict(engine_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        sample_main_sha256=hashlib.sha256(original.encode()).hexdigest(),cases=records)
    (output/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Wayfarer: hidden read/write-error and saved menu captures created; visual review pending')
