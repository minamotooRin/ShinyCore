"""Actual six-wave gameplay, seed repeatability, pause/upgrade boundaries and restart."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
root=Path(__file__).resolve().parents[1]
project=root/'examples/barrage'

def run(path,*args):
    result=subprocess.run([str(binary),str(path),'--headless',*args],capture_output=True,
                          text=True,encoding='utf-8',timeout=45)
    assert result.returncode==0,result.stderr
    return json.loads(result.stdout)

def write(path,events):
    path.write_text('\n'.join(json.dumps(row) for row in [{'version':3},*events])+'\n',encoding='utf-8')
    return str(path)

def event(frame,*keys):
    return {'frame':frame,'keys':list(keys),'gamepad':{'connected':False}}

with tempfile.TemporaryDirectory(prefix='shiny-barrage-') as directory:
    temp=Path(directory)
    events=[json.loads(line) for line in (project/'challenge.jsonl').read_text(encoding='utf-8').splitlines()][1:]
    # The authored keyboard replay plays all six full-duration waves in the shipped game.
    savedir=temp/'result'
    completed=run(project,'--frames','18138','--replay',str(project/'challenge.jsonl'),'--save-dir',str(savedir))
    game=completed['watches']['game']; result=completed['state']['result']
    assert game['mode']=='end' and game['won'] and game['wave']==6 and game['enemies']==0
    assert game['elapsed']>=300 and len(game['upgrades'])==5 and game['kills']==game['spawned']
    assert game['kills']>=300 and game['shots']>2000 and game['hits']>400 and game['dashes']>0
    assert result['won'] and result['seed']==260926 and result['upgrades']==game['upgrades']
    controller=run(project,'--frames','18138','--replay',str(project/'gamepad.jsonl'))
    assert controller['state']['result']==result and controller['watches']['game']==game
    def pad(frame,x=0,buttons=()):
        return {'frame':frame,'keys':[],'gamepad':{'connected':True,'buttons':list(buttons),'axes':{'left_x':x}}}
    start=pad(0);start.update(button_pressed=['south'],button_released=['south'])
    partial=run(project,'--frames','11','--replay',write(temp/'partial-stick.jsonl',[start,pad(1,.6)]))
    position=next(e for e in partial['entities'] if e['tag']=='player')
    assert abs(position['x']-(188+98*.5/60*10))<.0001 and position['y']==104
    held=run(project,'--frames','9','--replay',write(temp/'held-stick.jsonl',[
        start,pad(1,.6),pad(3,.6,['start']),pad(4,.6),pad(5,.6,['start']),pad(6,.6),pad(7),pad(8,.6)]))
    position=next(e for e in held['entities'] if e['tag']=='player')
    assert abs(position['x']-(188+98*.5/60*3))<.0001
    assert held['watches']['game']['dashes']==0 and held['watches']['game']['mode']=='game'
    # LAST RESULT restores only the authored score screen, then NEW CHALLENGE resets gameplay.
    load=[event(0),event(1,'tab'),event(2),event(3,'tab'),event(4),event(5,'enter'),event(6)]
    restored=run(project,'--frames','7','--replay',write(temp/'load-result.jsonl',load),'--save-dir',str(savedir))
    assert restored['state']['result']==result and restored['state']['show_result']
    watch=restored['watches']['game']
    assert watch['mode']=='end' and watch['won'] and watch['score']==game['score'] and watch['wave']==6
    assert watch['elapsed']==game['elapsed'] and watch['upgrades']==game['upgrades']
    restarted=run(project,'--frames','11','--replay',write(temp/'result-restart.jsonl',load+[
        event(7,'enter'),event(8),event(9,'enter'),event(10)]),'--save-dir',str(savedir))
    watch=restarted['watches']['game']
    assert watch['mode']=='game' and watch['wave']==1 and watch['score']==0 and watch['elapsed']<1
    assert not restarted['state']['show_result'] and restarted['state']['result']==result
    # A resume-frame stick/key must be neutral: menu-held actions stay consumed.
    # Compare with the same neutral gameplay frame, then insert a 60-frame pause.
    following=[]
    for row in events:
        if row['frame']<=101: following=row['keys']
    neutral=[row for row in events if row['frame']<100]
    neutral += [event(100),event(101,*following)]
    neutral += [row for row in events if row['frame']>101]
    reference=run(project,'--frames','18300','--replay',write(temp/'neutral-resume.jsonl',neutral))
    assert reference['watches']['game']['won']
    paused=[row for row in events if row['frame']<100]
    paused += [event(100,'escape'),event(101),event(160,'escape'),event(161,*following)]
    paused += [dict(row,frame=row['frame']+60) for row in events if row['frame']>101]
    # The same second run also exercises ending/restart without injecting completion.
    paused += [event(18360,'escape'),event(18361),event(18362,'enter'),event(18363)]
    repeated=run(project,'--frames','18364','--replay',write(temp/'pause-restart.jsonl',paused))
    assert repeated['state']['result']==reference['state']['result']
    watch=repeated['watches']['game']
    assert watch['mode']=='title' and watch['wave']==1 and watch['score']==0 and watch['kills']==0
    assert watch['health']==6 and watch['elapsed']==0 and not watch['won']
    # Aiming away from attackers makes the real damage/defeat path reachable.
    failure=run(project,'--frames','1800','--replay',write(temp/'failure.jsonl',[event(0,'enter'),event(1,'right')]))
    assert failure['watches']['game']['mode']=='end' and not failure['state']['result']['won']
    assert failure['watches']['game']['health']==0
    fixture=temp/'rules';fixture.mkdir();shutil.copyfile(project/'challenge.lua',fixture/'challenge.lua')
    (fixture/'main.lua').write_text('''local C=require('challenge')
return {init=function()
    local a,b=C.new(),C.new()
    for _=1,20 do assert(C.random(a,1,100)==C.random(b,1,100)) end
    local offers=C.offers(a); assert(#offers==3 and offers[1].id~=offers[2].id and offers[2].id~=offers[3].id)
    C.upgrade(a,'armor'); assert(a.health==8 and a.max_health==8)
    C.upgrade(a,'speed'); assert(a.speed==110)
    C.upgrade(a,'rate'); assert(a.interval<.24)
    C.upgrade(a,'spread'); assert(a.spread==3)
    a.health=1; C.upgrade(a,'repair'); assert(a.health==a.max_health)
    for _=1,3 do C.upgrade(a,'power') end
    local count=#a.upgrades; assert(not pcall(C.upgrade,a,'power') and #a.upgrades==count)
    assert(not pcall(C.upgrade,a,'unknown'))
    a.kills=11; a.health=1; C.defeat(a,10); assert(a.kills==12 and a.health==2 and a.score==10)
end}''',encoding='utf-8')
    run(fixture,'--frames','1')
print('Barrage: six waves, five upgrades, fixed-seed pause equivalence, defeat and fresh challenge passed')
