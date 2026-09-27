#!/usr/bin/env python3
"""Run the fixed combined barrage workload and enforce timing/count thresholds."""
import argparse
import json
from pathlib import Path
import subprocess
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,required=True,help='new directory; existing evidence is never overwritten')
parser.add_argument('--smoke',action='store_true',help='one short diagnostic run, not acceptance')
args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=False)
output=args.output.resolve()
binary=args.binary.resolve()
runs,frames,warmup,duration=(1,360,2,2) if args.smoke else (3,12720,30,180)
project=ROOT/'benchmarks/barrage'
profiles=[]
display_errors=[]
for run in range(1,runs+1):
    print(f'Barrage run {run}/{runs}: {frames} fixed updates',flush=True)
    profile=output/f'run-{run}.jsonl'
    command=[str(binary),str(project),'--frames',str(frames),'--replay',str(project/'smoke.txt'),
             '--profile',str(profile),'--capture',str(output/f'run-{run}.png')]
    with (output/f'run-{run}.json').open('w',encoding='utf-8') as result, (output/f'run-{run}.log').open('w',encoding='utf-8') as log:
        process=subprocess.run(command,stdout=result,stderr=log)
    if process.returncode:
        print(f'Engine failed; inspect {output}/run-{run}.log and .json',file=sys.stderr)
        raise SystemExit(1)
    snapshot=json.loads((output/f'run-{run}.json').read_text(encoding='utf-8'))
    if snapshot.get('frames') != frames:
        # Closing the native window is a successful application exit, but an
        # incomplete benchmark. Do not open another window after cancellation.
        error=f'run {run}: stopped after {snapshot.get("frames")} of {frames} required ticks'
        (output/'report.json').write_text(json.dumps({'ok':False,'scope':'incomplete benchmark',
            'errors':[error]},indent=2)+'\n',encoding='utf-8')
        print(error,file=sys.stderr)
        raise SystemExit(1)
    profiles.append(profile)
    with (output/f'run-{run}.png').open('rb') as image:
        header=image.read(24)
    if len(header)!=24 or header[:8]!=b'\x89PNG\r\n\x1a\n' or struct.unpack('>II',header[16:24])!=(1920,1080):
        display_errors.append(f'run {run}: captured output is not 1920x1080')
command=[sys.executable,str(ROOT/'tools/profile_report.py'),*map(str,profiles),
         '--warmup',str(warmup),'--duration',str(duration),'--runs',str(runs),
         '--cpu-p99-max','16.67','--gpu-p99-max','16.67','--rss-max-mib','512',
         '--minimum','entities=2000','--minimum','projectiles=20000','--minimum','particles=20000',
         '--min-hits-per-second','1000']
result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8')
if result.stderr: print(result.stderr,file=sys.stderr)
report=json.loads(result.stdout)
report['errors'].extend(display_errors)
report['ok']=report['ok'] and not display_errors
report['scope']='short diagnostic only' if args.smoke else 'barrage timing, load and process memory'
report['unverified']=['CPU/RAM hardware identity and clocks','graphics allocation budget',
                      'other four workloads and remaining complete-edition acceptance']
report['commands']={'report':command,'frames_per_run':frames}
(output/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'ok':report['ok'],'scope':report['scope'],'report':str(output/'report.json')}))
raise SystemExit(0 if report['ok'] else 1)
