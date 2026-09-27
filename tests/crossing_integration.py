"""Crossing's actual keyboard traversal, checkpoint reconstruction and mechanism rules."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
root=Path(__file__).resolve().parents[1]
project=root/'examples/crossing'

def run(path,*args):
    p=subprocess.run([str(binary),str(path),'--headless',*args],capture_output=True,text=True,encoding='utf-8',timeout=20)
    assert p.returncode==0,p.stderr
    return json.loads(p.stdout)

def replay(path,events):
    path.write_text('\n'.join(json.dumps(v) for v in [{'version':3},*[
        {'frame':i,'keys':keys,'gamepad':{'connected':False}} for i,keys in enumerate(events)]])+'\n',encoding='utf-8')
    return str(path)

with tempfile.TemporaryDirectory(prefix='shiny-crossing-') as directory:
    temp=Path(directory)
    run(project,'--check-all') # Every room can resolve application-owned input profiles.
    saved=run(project,'--frames','162','--replay',str(project/'checkpoint.jsonl'),'--save-dir',directory)
    campaign=saved['state']['campaign']
    assert campaign['started'] and campaign['room']==1 and not campaign['complete']
    assert campaign['stages'][0]['lights']=={'light.1':True}
    assert saved['watches']['crossing']['collected']==1
    assert len([e for e in saved['entities'] if e['tag']=='light'])==4
    load=replay(temp/'load.jsonl',[['tab'],[],['tab'],[],['tab'],[],['enter'],[]])
    restored=run(project,'--frames','10','--replay',load,'--save-dir',directory)
    assert restored['state']['campaign']==campaign
    assert restored['watches']['crossing']['mode']=='game' and restored['watches']['crossing']['collected']==1
    player=next(e for e in restored['entities'] if e['tag']=='player')
    assert abs(player['x']-campaign['stages'][0]['position']['x'])<.01
    assert not any(e['persistent_id']=='light.1' for e in restored['entities'])
    assert (temp/'shiny.crossing/checkpoint.json').is_file()
    # Verify rule branches independently of room traversal, using the shipped module.
    fixture=temp/'rules';fixture.mkdir()
    for name in ['campaign.lua','guide.lua','levels.lua']:shutil.copyfile(project/name,fixture/name)
    (fixture/'main.lua').write_text('''local C=require('campaign')
return {init=function()
    local c=C.new(); local first=c.stages[1]
    local G=require('guide')
    assert(G.current(1,first,24).label=='LIGHT' and G.current(1,first,24).x==240)
    assert(G.current(2,c.stages[2],24,315).x==315)
    assert(G.current(3,c.stages[3],24).label=='WEST [E]')
    C.interact(c,1,1); assert(not first.open)
    assert(C.collect(first,'light.1') and not C.collect(first,'light.1'))
    C.collect(first,'light.2'); assert(G.current(1,first,490).label=='LEVER')
    C.interact(c,1,1); assert(first.open and not C.ready(first))
    assert(G.current(1,first,610,nil,650).label=='FERRY' and G.current(1,first,610,nil,650).x==650)
    assert(G.current(1,first,780).label=='LIGHT')
    C.plate(c.stages[2],470,100); assert(not c.stages[2].open)
    C.plate(c.stages[2],470,192); assert(c.stages[2].open)
    C.interact(c,3,3); assert(c.stages[3].sequence==0)
    C.interact(c,3,1); C.interact(c,3,2); assert(c.stages[3].sequence==0)
    C.interact(c,3,1); assert(G.current(3,c.stages[3],300).label=='EAST [E]')
    C.interact(c,3,3); assert(G.current(3,c.stages[3],850).label=='CENTER [E]')
    C.interact(c,3,2); assert(c.stages[3].open)
    for _,stage in ipairs(c.stages) do for i=1,5 do C.collect(stage,'light.'..i) end; assert(C.ready(stage)) end
    assert(G.current(1,first,1100).label=='EXIT' and G.current(3,c.stages[3],1100).label=='BEACON')
    c.room=3; c.complete=true; c.started=true
    local restored=C.new(c); assert(restored.complete and restored.stages[3].sequence==3)
    restored.stages[1].lights['light.1']=nil; assert(not pcall(C.new,restored))
    c.version=99; assert(not pcall(C.new,c))
end}''',encoding='utf-8')
    run(fixture,'--frames','1')
    # Only recorded input drives the shipped game; no state or entity injection.
    trace=temp/'walkthrough.jsonl'
    completed=run(project,'--frames','2876','--replay',str(project/'walkthrough.jsonl'),
                  '--save-dir',directory,'--trace',str(trace))
    final=completed['state']['campaign']
    assert final['complete'] and final['room']==3 and final['stages'][2]['sequence']==3
    assert all(stage['open'] and len(stage['lights'])==5 and stage['deaths']==0 for stage in final['stages'])
    assert completed['watches']['crossing']['mode']=='end'
    visited=set(); music_ids=set(); ferry=plate=ramp=False
    for line in trace.read_text(encoding='utf-8').splitlines():
        frame=json.loads(line); visited.add(frame['scene'])
        music=[voice for voice in frame['audio'] if voice['path']=='assets/theme.ogg']
        assert len(music)==1
        music_ids.add(music[0]['id'])
        player=next(e for e in frame['entities'] if e['tag']=='player')
        if frame['scene']=='main.lua':
            platform=next(e for e in frame['entities'] if e['tag']=='platform')
            ferry |= player['grounded'] and player['support']==platform['id'] and 610<player['x']<690
        elif frame['scene']=='rooms/mill.lua':
            crate=next(e for e in frame['entities'] if e['tag']=='crate')
            plate |= 460<=crate['x']+8<=500 and 188<=crate['y']+16<=196
            slope=next(e for e in frame['entities'] if e['persistent_id']=='mill.ramp')
            ramp |= player['grounded'] and player['support']==slope['id']
    assert visited=={'main.lua','rooms/mill.lua','rooms/beacon.lua'}
    assert len(music_ids)==1,'music must retain one voice across all three rooms'
    assert ferry and plate and ramp,(ferry,plate,ramp)
    controller=run(project,'--frames','2876','--replay',str(project/'gamepad.jsonl'))
    assert controller['state']['campaign']==final,'controller traversal must match keyboard gameplay'
    assert controller['watches']['crossing']['mode']=='end'
    recovery=temp/'controller-recovery.jsonl'
    recovery.write_text('\n'.join(json.dumps(event) for event in [
        {'version':3},
        {'frame':0,'keys':[],'gamepad':{'connected':True,'buttons':['south']}},
        {'frame':1,'keys':[],'gamepad':{'connected':True,'buttons':['dpad_right']}},
        {'frame':25,'keys':[],'gamepad':{'connected':False}},
        {'frame':29,'keys':[],'gamepad':{'connected':True,'buttons':['north']}},
        {'frame':30,'keys':[],'gamepad':{'connected':True,'buttons':[]}},
    ])+'\n')
    recovered=run(project,'--frames','32','--replay',str(recovery))
    assert recovered['watches']['crossing']['deaths']==1
    assert abs(recovered['watches']['crossing']['player']-24)<.01
    end_state=run(project,'--frames','10','--replay',load,'--save-dir',directory)
    assert end_state['state']['campaign']==final
    assert end_state['watches']['crossing']['complete'] and end_state['watches']['crossing']['mode']=='end'
    restart=replay(temp/'restart.jsonl',[['tab'],[],['tab'],[],['tab'],[],['enter'],[],['escape'],[],['enter'],[]])
    restarted=run(project,'--frames','12','--replay',restart,'--save-dir',directory)
    assert restarted['scene']=='main.lua' and not restarted['watches']['crossing']['complete']
    assert restarted['watches']['crossing']['mode']=='title' and restarted['watches']['crossing']['collected']==0
    fall=replay(temp/'fall.jsonl',[['enter']]+[['d']]*429)
    fallen=run(project,'--frames','430','--replay',fall,'--save-dir',directory)
    assert fallen['watches']['crossing']['deaths']==1 and fallen['watches']['crossing']['collected']==2
    assert fallen['watches']['crossing']['player']<100
print('Crossing: three-room input traversal, ferry/plate/ramp, checkpoints, water recovery and ending/restart passed')
