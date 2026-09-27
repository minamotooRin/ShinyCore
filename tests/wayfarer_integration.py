"""Wayfarer journal, actual full quest, streamed checkpoint, ending and new journey."""
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
root=Path(__file__).resolve().parents[1]
project=root/'examples/wayfarer'
if not json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))['modules']['streaming']:
    print('wayfarer: streaming disabled; not verified')
    raise SystemExit(0)

def run(path,*args):
    result=subprocess.run([str(binary),str(path),'--headless',*args],capture_output=True,text=True,encoding='utf-8',timeout=30)
    if result.returncode: raise AssertionError(result.stderr)
    assert 'missing text glyph' not in result.stderr,result.stderr
    return json.loads(result.stdout)

def persistent_music(trace,scenes):
    frames=[json.loads(line) for line in trace.read_text(encoding='utf-8').splitlines()]
    assert set(frame['scene'] for frame in frames)==set(scenes)
    voices=[frame['audio'] for frame in frames]
    assert all(len(voice)==1 and voice[0]['path']=='assets/theme.ogg' for voice in voices)
    assert len({voice[0]['id'] for voice in voices})==1
    positions=[voice[0]['position'] for voice in voices]
    # Room changes cannot restart the 20-second theme during this short trace.
    assert all(math.isclose(b-a,1/60,abs_tol=1e-4) for a,b in zip(positions,positions[1:])),positions
    return frames

with tempfile.TemporaryDirectory(prefix='shiny-wayfarer-') as folder:
    temp=Path(folder)
    looped=run(project,'--frames','1201','--save-dir',str(temp/'music-loop'))
    assert len(looped['audio'])==1 and 0<looped['audio'][0]['position']<.05
    saved=run(project,'--frames','45','--replay',str(project/'journal.jsonl'),'--save-dir',folder)
    assert saved['state']['quest_stage']=='gather' and saved['state']['equipment']=='field'
    assert saved['state']['traveler']=='小林'
    assert (temp/'shiny.wayfarer/checkpoint.json').is_file()
    trace=temp/'controller-journal-trace.jsonl'
    journal=run(project,'--frames','22','--replay',str(project/'controller-journal.jsonl'),
                '--save-dir',str(temp/'controller-journal'),'--trace',str(trace))
    assert journal['state']['traveler']=='旅人i'
    frames=[json.loads(line) for line in trace.read_text(encoding='utf-8').splitlines()]
    for i in [4,5,9,10,11,12]: assert frames[i]['watches']['quest']['mode']=='inventory'
    for i in [6,7,8,13,14]: assert frames[i]['watches']['quest']['mode']=='game'
    def player_x(frame):return next(e['x'] for e in frame['entities'] if e['tag']=='player')
    assert player_x(frames[6])==player_x(frames[14]) and player_x(frames[-1])>player_x(frames[14])
    replay=temp/'load.jsonl'
    events=[(0,['tab']),(1,[]),(2,['tab']),(3,[]),(4,['tab']),(5,[]),(6,['enter']),(7,[]),(8,['enter']),(9,[])]
    replay.write_text('\n'.join([json.dumps({'version':3})]+[json.dumps({'frame':frame,'keys':keys,'gamepad':{'connected':False}}) for frame,keys in events])+'\n')
    restored=run(project,'--frames','12','--replay',str(replay),'--save-dir',folder)
    assert restored['state']==saved['state']
    assert abs(saved['state']['courier']['x']-324)>1
    assert restored['watches']['quest']['stage']=='gather' and restored['watches']['quest']['equipment']=='field'
    # A fresh slot and only actual key input drive the complete forest route.
    journey=temp/'journey'
    completed=run(project,'--frames','3243','--replay',str(project/'walkthrough.jsonl'),'--save-dir',str(journey))
    assert completed['watches']['quest']['mode']=='end' and completed['watches']['quest']['complete']
    assert completed['state']['herbs']==24 and completed['state']['route_cleared']
    controller=run(project,'--frames','3243','--replay',str(project/'gamepad.jsonl'),'--save-dir',str(temp/'controller'))
    assert controller['state']==completed['state'] and controller['watches']==completed['watches']
    slot=journey/'shiny.wayfarer/checkpoint.json'
    data=json.loads(slot.read_text(encoding='utf-8'))
    authored={f'forest:{x}:{y}' for x in range(4) for y in range(4)}
    assert authored<=data['chunks'].keys() # Interest padding may also save empty boundary chunks.
    records={key:json.loads((slot.with_name(slot.name+'.chunks')/entry['file']).read_text(encoding='utf-8'))
             for key,entry in data['chunks'].items() if key in authored}
    assert sum(record.get('deleted') is True for chunk in records.values() for record in chunk['objects'].values())==24
    assert records['forest:1:0']['extra']['tiles']['0']['522']==4
    def keys(name,events):
        path=temp/name
        path.write_text('\n'.join([json.dumps({'version':3})]+[
            json.dumps({'frame':i,'keys':value,'gamepad':{'connected':False}}) for i,value in enumerate(events)])+'\n',encoding='utf-8')
        return str(path)
    loaded=run(project,'--frames','4','--replay',keys('ending.jsonl',[['enter'],[]]),'--save-dir',str(journey))
    assert loaded['state']==completed['state']
    assert loaded['watches']['quest']['mode']=='end' and not any(e['tag']=='herb' for e in loaded['entities'])
    # The title owns no stream world; cancelling a new journey keeps the entire slot.
    before=slot.read_bytes()
    # The restored score screen is visible before state/image preparation completes.
    # Wait for that initialization before requesting another room.
    menu=[['enter'],[],[],[],['enter'],[],['tab'],[],['tab'],[],['enter'],[]]
    cancel_trace=temp/'cancel-trace.jsonl'
    canceled=run(project,'--frames','14','--replay',keys('cancel.jsonl',menu+[['enter'],[]]),
                 '--save-dir',str(journey),'--trace',str(cancel_trace))
    assert canceled['scene']=='title.lua' and canceled['watches']['quest']['mode']=='title', (canceled['scene'],canceled['watches'])
    assert not canceled['entities'] and slot.read_bytes()==before
    persistent_music(cancel_trace,['main.lua','title.lua'])
    # The new room publishes its initial asynchronous state read on the next tick.
    new_trace=temp/'new-trace.jsonl'
    fresh=run(project,'--frames','19','--replay',keys('new.jsonl',menu+[['tab'],[],['enter'],[]]),
              '--save-dir',str(journey),'--trace',str(new_trace))
    assert fresh['scene']=='main.lua' and fresh['watches']['quest']['stage']=='meet'
    scenes=[frame['scene'] for frame in persistent_music(new_trace,['main.lua','title.lua'])]
    assert scenes.index('title.lua')<len(scenes)-1 and scenes[-1]=='main.lua'
    assert fresh['watches']['quest']['collected']==0 and not fresh['watches']['stream']['route_cleared']
    assert any(e['tag']=='herb' for e in fresh['entities']) and not slot.exists()
    fixture=temp/'rules';fixture.mkdir()
    shutil.copyfile(project/'quest.lua',fixture/'quest.lua')
    shutil.copyfile(project/'assets/SourceHanSansSC-Regular.otf',fixture/'ui.otf')
    (fixture/'project.lua').write_text('return {resources={ui={type="font",path="ui.otf",size=16}}}',encoding='utf-8')
    (fixture/'main.lua').write_text('''local Q=require("quest")
return {init=function()
local stage,words,done=Q.talk("meet",0,false); assert(stage=="gather" and not done)
assert(Q.objective(stage,23,true):find("23 / 24",1,true))
stage,words,done=Q.talk(stage,24,false); assert(stage=="gather" and not done)
stage,words,done=Q.talk(stage,24,true); assert(stage=="complete" and done)
assert(select(3,Q.talk(stage,24,true))==false)
assert(Q.equipment.trail.speed>Q.equipment.field.speed and Q.equipment.trail.reach<Q.equipment.field.reach)
end,draw=function()
local texts={Q.equipment.trail.label,Q.equipment.field.label,"小林"}
for _,state in ipairs({"meet","gather","complete"}) do
 for _,herbs in ipairs({0,24}) do for _,cleared in ipairs({false,true}) do
  texts[#texts+1]=Q.objective(state,herbs,cleared)
  texts[#texts+1]=select(2,Q.talk(state,herbs,cleared))
 end end
end
for _,text in ipairs(texts) do
 assert(sc.measure(text,14,"ui")>0)
 sc.text(text,0,0,14,"#FFFFFFFF",true,{font="ui"})
end
end}''',encoding='utf-8')
    run(fixture,'--frames','1')
print('Wayfarer: full forest quest, 16 saved chunks, ending restore, music continuity, new-journey isolation, journal and Chinese text passed')
