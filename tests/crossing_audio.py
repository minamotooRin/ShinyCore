"""Crossing cue triggers and reproducible local assets through the real host."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
root=Path(__file__).resolve().parents[1]
project=root/'examples/crossing'
spec=importlib.util.spec_from_file_location('crossing_audio',root/'tools/build_crossing_audio.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)

def rows(events):
    return '\n'.join(json.dumps(row) for row in [{'version':3},*[
        {'frame':frame,'keys':keys,'gamepad':{'connected':False}} for frame,keys in events]])+'\n'

with tempfile.TemporaryDirectory(prefix='shiny-crossing-audio-') as directory:
    temp=Path(directory)
    builder.build(temp/'cues')
    for name in builder.CUES:
        assert (project/'assets'/f'{name}.wav').read_bytes()==(temp/'cues'/f'{name}.wav').read_bytes(),name
    def run(name,frames,events=None):
        replay=project/'walkthrough.jsonl' if events is None else temp/f'{name}.jsonl'
        if events is not None:replay.write_text(rows(events),encoding='utf-8')
        process=subprocess.run([str(binary),str(project),'--headless','--frames',str(frames),
            '--replay',str(replay),'--save-dir',str(temp/name)],capture_output=True,text=True,encoding='utf-8',timeout=20)
        assert process.returncode==0,process.stderr
        return json.loads(process.stdout)
    jump=[(0,['enter']),(1,[]),(8,['space']),(9,[])]
    early=run('jump',10,jump)
    assert 'assets/jump.wav' in {voice['path'] for voice in early['audio']}
    assert 'assets/land.wav' not in {voice['path'] for voice in early['audio']}
    landed=run('land',62,jump)
    assert 'assets/land.wav' in {voice['path'] for voice in landed['audio']}
    rescue=run('rescue',4,[(0,['enter']),(1,[]),(2,['r']),(3,[])])
    assert rescue['watches']['crossing']['deaths']==1
    assert 'assets/rescue.wav' in {voice['path'] for voice in rescue['audio']}
    switch=run('switch',333)
    assert switch['watches']['crossing']['open']
    assert 'assets/switch.wav' in {voice['path'] for voice in switch['audio']}
    authored=[json.loads(line) for line in (project/'walkthrough.jsonl').read_text(encoding='utf-8').splitlines()][1:]
    wrong=[(row['frame'],[] if row['frame']==1857 else row['keys']) for row in authored]
    rejected=run('error',2255,wrong)
    assert rejected['watches']['crossing']['sequence']==0
    assert 'assets/error.wav' in {voice['path'] for voice in rejected['audio']}
print('Crossing audio: reproducible cues, jump/landing, rescue and switch outcomes passed')
