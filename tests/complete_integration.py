"""Behavioral checks for new systems and portable Lua modules."""
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

binary=Path(sys.argv[1]).resolve()
root=Path(sys.argv[2]).resolve()

with tempfile.TemporaryDirectory(prefix="shiny-complete-") as directory:
    project=Path(directory)
    shutil.copytree(root/"lua/shiny",project/"lib/shiny")
    (project/"project.lua").write_text('return {id="test.complete",limits={entities=8,particles=32,draws=128}}',encoding="utf-8")
    (project/"main.lua").write_text(r'''
local Edit=require("shiny.textedit")
local Tween=require("shiny.tween")
local Prefab=require("shiny.prefab")
local UI=require("shiny.ui")
local ui
return {gravity=0,init=function()
    local a=sc.spawn({x=10,y=10,w=4,h=20,tag="target"})
    local b=sc.spawn({x=20,y=10,tag="target"})
    assert(#sc.find_all("target")==2)
    assert(not pcall(sc.set_many,{{id=a,patch={x=30}},{id=b,patch={w=-1}}}))
    assert(sc.get(a).x==10)
    sc.set_many({{id=a,patch={x=40}},{id=b,patch={x=60}}})
    local copies=sc.get_many({a,b}); assert(copies[1].x==40 and copies[2].x==60)
    local field,status=sc.navigation.flow(10,10)
    assert(status=="ok")
    sc.navigation.steer(field,{a,b},12)
    assert(sc.get(a).vx~=0 or sc.get(a).vy~=0)
    local vx=sc.get(a).vx
    assert(not pcall(sc.navigation.steer,field,{a,0},99))
    assert(sc.get(a).vx==vx)
    sc.set_many({{id=a,patch={vx=0,vy=0}},{id=b,patch={vx=0,vy=0}}})
    local merged=Prefab.merge({body={type="dynamic",friction=1},items={1,2}}, {body={friction=0},items={3}})
    assert(merged.body.type=="dynamic" and merged.body.friction==0 and #merged.items==1)
    local e=Edit.new("a\u{0301}中")
    Edit.erase(e,-1); assert(e.value=="a\u{0301}")
    Edit.erase(e,-1); assert(e.value=="")
    Edit.undo(e); assert(e.value=="a\u{0301}")
    Edit.select_all(e); Edit.insert(e,"new"); assert(e.value=="new")
    local target={x=0}; local tween=Tween.new(target,{x=10},1,"linear")
    Tween.update(tween,.5); assert(target.x==5); Tween.update(tween,.5); assert(tween.done and target.x==10)
    sc.projectiles.configure(8)
    sc.projectiles.spawn({{x=0,y=15,vx=3000,terrain=false}})
    ui=UI.new({id="root",children={{id="label",kind="label",text="Complete systems"}}})
    UI.layout(ui,384,216)
end,update=function(dt)
    if sc.tick()==1 then assert(sc.projectiles.count()==0 and #sc.projectiles.hits()==1) end
    sc.debug.watch("ui",UI.inspect(ui))
    sc.debug.watch("verified",true)
end,draw=function() UI.draw(ui) end}
''',encoding="utf-8")
    result=subprocess.run([str(binary),'--headless',str(project),'--frames','3'],capture_output=True,text=True,encoding="utf-8")
    if result.returncode: raise AssertionError(result.stderr)
    state=json.loads(result.stdout)
    assert state['watches']['verified'] and state['projectile_hits']==1

modules=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))['modules']
for name in ('crossing','wayfarer','barrage'):
    if name=='wayfarer' and not modules['streaming']:
        print('wayfarer: requires streaming build; sample smoke not run')
        continue
    project=root/'examples'/name
    with tempfile.TemporaryDirectory(prefix='shiny-sample-saves-') as saves:
        for arguments in (['--check-all'],['--headless','--frames','180','--replay',str(project/'smoke.jsonl'),'--save-dir',saves]):
            result=subprocess.run([str(binary),str(project),*arguments],capture_output=True,text=True,encoding="utf-8",timeout=30)
            if result.returncode: raise AssertionError(f'{name}: {result.stderr}')
print('complete integration: batch atomicity, graphemes, modules, projectiles and available sample smoke passed')
