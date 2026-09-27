"""Persistent hierarchy sample: save, detach, room reset and restore; optional native image."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--capture',type=Path)
args=parser.parse_args();binary=args.binary.resolve()
root=Path(__file__).resolve().parents[1];project=root/'examples/attachments'
with tempfile.TemporaryDirectory(prefix='shiny-hierarchy-save-') as folder:
    saves=Path(folder)
    def run(native=False):
        command=[str(binary),str(project),'--frames','40','--replay',str(project/'smoke.jsonl'),
                 '--save-dir',str(saves/('native' if native else 'headless'))]
        if native:
            args.capture.mkdir(parents=True,exist_ok=True)
            command+=['--capture-hidden','--mute','--capture',str(args.capture.resolve()/'restored.png')]
        else:command+=['--headless']
        result=subprocess.run(command,capture_output=True,timeout=30)
        assert result.returncode==0,result.stderr.decode('utf-8',errors='replace')
        snapshot=json.loads(result.stdout)
        record=json.loads((saves/('native' if native else 'headless')/'shiny.attachments/rig.json').read_text(encoding='utf-8'))
        assert snapshot['watches']['rig']==record['state']['rig']
        assert snapshot['watches']['paused']
        rows=record['state']['rig']['objects']
        assert [row['parent'] for row in rows]==['rig/body','rig/lamp',False]
        assert len({e['id'] for e in snapshot['entities']})==3
        assert all(e['id']>>32>1 for e in snapshot['entities']) # Room rebuild, not old handles.
        assert all(set(row)=={'persistent_id','parent','x','y','angle','vx','vy','angular_velocity'} for row in rows)
        if args.capture:
            args.capture.mkdir(parents=True,exist_ok=True)
            name='native' if native else 'headless'
            (args.capture/(name+'.json')).write_bytes(result.stdout)
            (args.capture/(name+'.log')).write_bytes(result.stderr)
            (args.capture/(name+'-save.json')).write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
        return snapshot,command
    fixed,_=run()
    if args.capture:
        native,command=run(True)
        for field in ['entities','watches','hash','rng','tick']:assert fixed[field]==native[field],field
        (args.capture/'manifest.json').write_text(json.dumps({'command':command,
            'engine_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'checks':['persistent names only','saved and restored explicit fields equal','new room handles',
                      'headless/native entities, watches, hash, rng and tick equal'],
            'visual_review':'pending'},indent=2)+'\n',encoding='utf-8')
print('persistent attachments: saved names/local poses, reset/load and regenerated handles passed')
