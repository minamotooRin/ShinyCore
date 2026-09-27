"""Barrage's real defeat/result flow uses a disk transaction and restores it."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
project=Path(__file__).resolve().parents[1]/'examples/barrage'

def run(replay,frames,directory):
    result=subprocess.run([str(binary),str(project),'--headless','--frames',str(frames),
        '--replay',str(replay),'--save-dir',str(directory)],capture_output=True,text=True,
        encoding='utf-8',timeout=35)
    assert result.returncode==0,result.stderr
    return json.loads(result.stdout)

with tempfile.TemporaryDirectory(prefix='shiny-barrage-result-') as folder:
    root=Path(folder)
    saved=run(project/'failure.jsonl',1800,root/'saved')
    assert saved['watches']['game']['mode']=='end' and saved['state']['result']['won'] is False
    assert saved['watches']['result_io']['status']=='saved'
    assert (root/'saved/shiny.barrage/last_result.json').is_file()
    replay=root/'load.jsonl'
    def event(frame,keys): return {'frame':frame,'keys':keys,'gamepad':{'connected':False}}
    replay.write_text('\n'.join(json.dumps(row) for row in [
        {'version':3},
        event(0,[]),event(1,['tab']),event(2,[]),event(3,['tab']),
        event(4,[]),event(5,['enter']),event(6,[]),
    ])+'\n',encoding='utf-8')
    restored=run(replay,7,root/'saved')
    assert restored['watches']['game']['mode']=='end' and restored['state']['show_result']
    assert restored['watches']['result_io']['status']=='restored'
    assert restored['state']['result']==saved['state']['result']
    blocked=root/'blocked/shiny.barrage/last_result.json'
    blocked.mkdir(parents=True)
    unsaved=run(project/'failure.jsonl',1800,root/'blocked')
    assert unsaved['watches']['game']['mode']=='end' and unsaved['state']['result']['won'] is False
    assert unsaved['watches']['result_io']['status']=='failed'
    assert blocked.is_dir()
print('Barrage: async disk result, restored result and failed-write recovery passed')
