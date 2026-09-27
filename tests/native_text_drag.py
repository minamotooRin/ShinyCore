"""Capture a bounded, hidden native view of multiline selection auto-scroll."""
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
binary=args.binary.resolve()
output=args.output.resolve()
output.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1]
project=output/'project'
project.mkdir()
shutil.copytree(root/'lua/shiny',project/'lib/shiny')
(project/'project.lua').write_text('return {display={width=1152,height=648,vsync=false}}',encoding='utf-8')
value='\n'.join(f'FIELD NOTE {i:02d}' for i in range(60))
source='''local UI=require('shiny.ui')
local Edit=require('shiny.textedit')
local node={id='journal',kind='input',multiline=true,x=22,y=50,w=232,h=72,value=VALUE}
local ui=UI.new({id='page',kind='overlay',padding=0,children={node}})
return {width=384,height=216,ambient=1,init=function() UI.layout(ui,384,216) end,update=function(dt)
    UI.update(ui,dt,384,216)
    sc.debug.watch('cursor',node.editor.cursor)
    sc.debug.watch('selected',#Edit.selected(node.editor))
    sc.debug.watch('scroll',node.text_scroll_y)
end,draw=function()
    sc.rect(0,0,384,216,'#111C2E',true)
    sc.text('DRAG SELECTION',22,18,20,'#E6EDF7',true)
    UI.draw(ui)
    sc.text('Held pointer below the viewport',22,148,12,'#FFCB77',true)
end}'''
(project/'main.lua').write_text(source.replace('VALUE',json.dumps(value)),encoding='utf-8')
events=[{'version':2}]
for frame in range(16):
    events.append({'frame':frame,'keys':[],
        'gamepad':{'connected':False,'buttons':[],'axes':{}},
        'mouse':{'x':30,'y':60 if frame==0 else 1000,'inside':frame==0,'buttons':['left']}})
replay=output/'drag.jsonl'
replay.write_text('\n'.join(json.dumps(row) for row in events)+'\n',encoding='utf-8')
capture=output/'drag.png'
command=[str(binary),str(project),'--frames','16','--capture-hidden','--capture',str(capture),
         '--mute','--replay',str(replay),'--save-dir',str(output/'saves')]
result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=30)
(output/'run.log').write_text(result.stderr,encoding='utf-8')
if result.returncode: raise RuntimeError(result.stderr)
snapshot=json.loads(result.stdout)
watch=snapshot['watches']
if not (0<watch['selected']<len(value) and 0<watch['scroll']<len(value)):
    raise RuntimeError(f'bounded drag selection failed: {watch}')
(output/'snapshot.json').write_text(result.stdout,encoding='utf-8')
report={'engine_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
        'capture':capture.name,'frames':16,'watches':watch,'visual_review':'pending'}
(output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(capture)
