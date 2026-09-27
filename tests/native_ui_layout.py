"""Hidden two-frame native check of incremental UI layout after geometry/text changes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
binary=args.binary.resolve();output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1];project=output/'project';project.mkdir()
shutil.copytree(root/'lua/shiny',project/'lib/shiny')
(project/'project.lua').write_text('return {display={width=960,height=540,vsync=false}}')
(project/'main.lua').write_text('''local UI=require('shiny.ui')
local ui=UI.new{id='root',kind='row',x=12,y=40,w=360,h=148,padding=0,gap=12,children={
    {id='left',kind='column',w=174,h=148,padding=8,gap=8,background='#1D2C43',children={
        {id='title',text='Changed panel',h=20},
        {id='grow',kind='button',text='Expanded',h=28},
        {id='hide',kind='button',text='Hidden after update',h=28},
        {id='tail',kind='button',text='Moved below',h=28}}},
    {id='right',kind='column',w=174,h=148,padding=8,gap=8,background='#1D2C43',children={
        {id='stable',text='Unchanged panel',h=20},
        {id='status',kind='button',text='Before',h=28},
        {id='note',text='Geometry reused',h=28}}}}}
local stable
return {width=384,height=216,ambient=1,init=function()
    UI.layout(ui,384,216);stable=ui.nodes.status.rect;ui.focus='tail'
end,update=function(dt)
    if sc.tick()==1 then
        UI.set(ui,'grow',{h=60});UI.set(ui,'hide',{visible=false})
        UI.set(ui,'status',{text='Text updated',color='#66D9B0'})
    end
    UI.update(ui,dt,384,216)
    if sc.tick()==1 then
        assert(ui.nodes.status.rect==stable and ui.nodes.status.text_layout.text=='Text updated')
        assert(ui.nodes.tail.rect.y==144 and ui.nodes.grow.rect.h==60)
        sc.debug.watch('independent_geometry_reused',true)
        sc.debug.watch('hidden',ui.nodes.hide.visible==false)
    end
end,draw=function()
    sc.rect(0,0,384,216,'#111C2E',true)
    sc.text('Local layout update',12,12,18,'#E6EDF7',true)
    UI.draw(ui)
    sc.text('Resize + hide on left; text only on right.',12,198,10,'#8CA4C4',true)
end}
''',encoding='utf-8')
replay=output/'input.jsonl';replay.write_text('{"version":3}\n{"frame":0,"keys":[],"gamepad":{"connected":false}}\n')
command=[str(binary),str(project),'--frames','2','--capture-hidden','--capture',str(output/'layout.png'),
         '--mute','--replay',str(replay),'--save-dir',str(output/'saves')]
result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=20)
(output/'layout.log').write_text(result.stderr,encoding='utf-8')
assert result.returncode==0,result.stderr
assert json.loads(result.stdout)['watches']==dict(independent_geometry_reused=True,hidden=True)
(output/'layout.json').write_text(result.stdout,encoding='utf-8')
(output/'manifest.json').write_text(json.dumps(dict(command=command,
    engine_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
    ui_sha256=hashlib.sha256((project/'lib/shiny/ui.lua').read_bytes()).hexdigest(),
    checks='passed',visual_review='pending'),indent=2)+'\n',encoding='utf-8')
print(output/'layout.png')
