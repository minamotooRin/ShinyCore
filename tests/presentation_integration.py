"""Short fixed-replay comparison: interpolation must not affect gameplay or saves."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
functions={entry['name']:entry for entry in api['functions']}
assert functions['sc.presentation.interpolate']['contract']['phases']==['load','init','update']
assert 'draw' in functions['sc.presentation.pose']['contract']['phases']
with tempfile.TemporaryDirectory(prefix='shiny-presentation-') as directory:
    project=Path(directory)
    (project/'project.lua').write_text('return {id="presentation.test",limits={entities=8,particles=8,projectiles=8}}',encoding='utf-8')
    source='''local id
return {gravity=0,init=function()
sc.presentation.interpolate(ENABLED)
sc.camera.set{bounds=false,pixel_snap=false,smoothing=1}
id=sc.spawn{x=100,y=100,w=10,h=10,vx=120,tag="player"}
sc.camera.follow(id)
sc.projectiles.configure(8)
sc.projectiles.spawn{{x=0,y=0,vx=120,life=.05,mask=0,terrain=false}}
sc.emit(0,0,3,"#FFFFFF",10,.05)
end,update=function()
sc.set(id,{vx=sc.input.key_down("d") and 120 or 60})
sc.state.set("position",sc.get(id).x)
if sc.tick()==2 then sc.set(id,{x=800});sc.presentation.snap(id);sc.presentation.snap_camera() end
if sc.tick()==4 then assert(sc.save.write("slot")) end
end,draw=function(alpha)
assert(alpha==1) -- Bounded/headless execution always presents current state.
local p=sc.presentation.pose(id);assert(p.x==sc.get(id).x)
sc.circle(p.x,p.y,4,"#FFFFFF")
end}
'''
    replay=project/"input.jsonl"
    replay.write_text("\n".join(json.dumps(row) for row in ({"version":3},{"frame":0,"keys":["d"],"gamepad":{"connected":False}},{"frame":3,"keys":[],"gamepad":{"connected":False}}))+"\n",encoding="utf-8")
    outputs=[]; saves=[]
    for enabled in ('false','true'):
        (project/'main.lua').write_text(source.replace('ENABLED',enabled),encoding='utf-8')
        save_dir=project/enabled
        result=subprocess.run([str(binary),str(project),'--headless','--frames','6','--replay',str(replay),'--save-dir',str(save_dir)],capture_output=True,text=True,encoding='utf-8',timeout=15)
        assert result.returncode==0,result.stderr
        outputs.append(json.loads(result.stdout))
        saves.append(json.loads((save_dir/'presentation.test/slot.json').read_text(encoding='utf-8')))
    for field in ('hash','state_hash','state','entities','camera','rng','projectiles','projectile_hits','audio','input'):
        assert outputs[0][field]==outputs[1][field],field
    # Save metadata may contain a wall clock; the explicitly persisted gameplay is identical.
    assert saves[0]['state']==saves[1]['state']
print('presentation enabled/disabled: fixed replay fields and save state match')
