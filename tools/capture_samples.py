#!/usr/bin/env python3
"""Capture selected sample scenes through the actual hidden native renderer."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
CASES={
    'wayfarer-title':('wayfarer',None,4),
    'wayfarer-dialogue':('wayfarer','journal.jsonl',27),
    'wayfarer-journal':('wayfarer','journal.jsonl',38),
    'wayfarer-world':('wayfarer','travel.jsonl',182),
    'wayfarer-gather-ready':('wayfarer','walkthrough.jsonl',187),
    'wayfarer-gathered':('wayfarer','walkthrough.jsonl',194),
    'wayfarer-camp':('wayfarer','walkthrough.jsonl',24),
    'wayfarer-trail':('wayfarer','walkthrough.jsonl',114),
    'crossing-ferry':('crossing','walkthrough.jsonl',435),
    'crossing-aqueduct':('crossing',None,10),
    'crossing-mill':('crossing',None,10),
    'crossing-signal':('crossing',None,10),
    'barrage-wave':('barrage','challenge.jsonl',180),
    'crossing-ending':('crossing',None,10),
    'wayfarer-ending':('wayfarer',None,4),
    'barrage-ending':('barrage',None,6),
    'barrage-defeat':('barrage',None,6),
}
# Reach the real checkpoint through authored input, then render its normal restore.
PREPARE={
    'crossing-ending':('walkthrough.jsonl',2876,[['tab'],[],['tab'],[],['tab'],[],['enter'],[]]),
    'wayfarer-ending':('walkthrough.jsonl',3245,[['enter'],[]]),
    'barrage-ending':('challenge.jsonl',18138,[['tab'],[],['tab'],[],['enter'],[]]),
    'barrage-defeat':('failure.jsonl',1800,[['tab'],[],['tab'],[],['enter'],[]]),
}
CHECKPOINTS={'crossing-aqueduct':(435,1),'crossing-mill':(1050,2),'crossing-signal':(2255,3)}
DEFAULT_CASES=[name for name in CASES if name not in PREPARE and name not in CHECKPOINTS]

def check_ending(snapshot,sample,name):
    watch=snapshot['watches'][{'crossing':'crossing','wayfarer':'quest','barrage':'game'}[sample]]
    finished=watch.get('won')==(name=='barrage-ending') if sample=='barrage' else watch.get('complete') is True
    if watch['mode']!='end' or not finished:
        raise RuntimeError(f'{name}: expected ending was not reached: {watch}')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--case',dest='cases',action='append',choices=CASES,
                        help='Repeat to capture changed scenes; ending replays are opt-in')
    parser.add_argument('--project',type=Path,
                        help='Use a relocated sample project; requires cases belonging to one sample')
    args=parser.parse_args()
    selected=args.cases or DEFAULT_CASES
    if args.project and (not args.cases or len({CASES[name][0] for name in selected})!=1):
        parser.error('--project requires --case selections from exactly one sample')
    binary=args.binary.resolve()
    with binary.open('rb') as source:engine_hash=hashlib.file_digest(source,'sha256').hexdigest()
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    report=[]
    with tempfile.TemporaryDirectory(prefix='shiny-captures-') as directory:
        temporary=Path(directory)
        idle=temporary/'idle.jsonl'
        idle.write_text('{"version":3}\n{"frame":0,"keys":[],"gamepad":{"connected":false}}\n',encoding='utf-8')
        for name in selected:
            sample,replay,frames=CASES[name]
            project=args.project.resolve() if args.project else ROOT/'examples'/sample
            capture=output/f'{name}.png'
            replay_path=project/replay if replay else idle
            save_dir=output/f'{name}-saves' if name in PREPARE or name in CHECKPOINTS else temporary/name
            preparation=None
            if name in PREPARE or name in CHECKPOINTS:
                if name in CHECKPOINTS:
                    ticks,room=CHECKPOINTS[name]
                    # Reach the room using real controls, then press the game's quick-save key.
                    original=project/'walkthrough.jsonl'
                    rows=[json.loads(line) for line in original.read_text(encoding='utf-8').splitlines() if line.strip()]
                    rows=[row for row in rows if 'frame' not in row or row['frame']<ticks]
                    final={**rows[-1],'frame':ticks-1,'keys':list(dict.fromkeys([*rows[-1]['keys'],'f6']))}
                    if rows[-1]['frame']==ticks-1: rows[-1]=final
                    else: rows.append(final)
                    source=output/f'{name}-checkpoint.jsonl'
                    source.write_text('\n'.join(json.dumps(row) for row in rows)+'\n',encoding='utf-8')
                    keys=[['tab'],[],['tab'],[],['tab'],[],['enter'],[]]
                else:
                    source,ticks,keys=PREPARE[name]
                prepared=subprocess.run([str(binary),str(project),'--headless','--frames',str(ticks),
                    '--replay',str(project/source),'--save-dir',str(save_dir)],
                    capture_output=True,text=True,encoding='utf-8',timeout=60)
                (output/f'{name}-prepare.log').write_text(prepared.stderr,encoding='utf-8')
                if prepared.returncode: raise RuntimeError(f'{name} preparation: {prepared.stderr}')
                (output/f'{name}-prepare.json').write_text(prepared.stdout,encoding='utf-8')
                prepared_data=json.loads(prepared.stdout)
                if name in CHECKPOINTS:
                    watch=prepared_data['watches']['crossing']
                    if watch['room']!=room or watch['mode']!='game': raise RuntimeError(f'{name}: checkpoint not reached: {watch}')
                else: check_ending(prepared_data,sample,name)
                replay_path=output/f'{name}-restore.jsonl'
                replay_path.write_text('\n'.join(json.dumps(row) for row in [{'version':3},*[
                    {'frame':i,'keys':value,'gamepad':{'connected':False}} for i,value in enumerate(keys)]])+'\n',encoding='utf-8')
                preparation={'replay':str(source),'frames':ticks,'restore_replay':replay_path.name,'save_dir':save_dir.name}
            command=[str(binary),str(project),'--frames',str(frames),'--mute',
                     '--capture-hidden','--capture',str(capture),'--replay',str(replay_path),
                     '--save-dir',str(save_dir)]
            result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=30)
            (output/f'{name}.log').write_text(result.stderr,encoding='utf-8')
            if result.returncode: raise RuntimeError(f'{name}: {result.stderr}')
            snapshot=json.loads(result.stdout)
            if name in CHECKPOINTS:
                watch=snapshot['watches']['crossing']
                if watch['room']!=room or watch['mode']!='game': raise RuntimeError(f'{name}: checkpoint restore failed: {watch}')
            elif preparation: check_ending(snapshot,sample,name)
            (output/f'{name}.json').write_text(result.stdout,encoding='utf-8')
            report.append({'case':name,'frames':snapshot['frames'],'capture':capture.name,
                           'engine_sha256':engine_hash,'replay':replay,
                           'preparation':preparation,
                           'watches':snapshot['watches'],'visual_review':'pending'})
            print(f'{name}: {capture}',flush=True)
            (output/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (output/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__': main()
