"""Opt-in hidden native preview of all six IME conversion states; no live input."""
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
(project/'project.lua').write_text('return {display={width=1280,height=480,vsync=false}}')
(project/'main.lua').write_text('''local UI=require('shiny.ui')
local Edit=require('shiny.textedit')
local theme={};for k,v in pairs(UI.theme) do theme[k]=v end
theme.font_size=20
local entry={id='entry',kind='input',x=24,y=64,w=592,h=48,value='Unchanged'}
local ui=UI.new({id='root',kind='overlay',padding=0,children={entry}},theme)
return {width=640,height=240,ambient=1,init=function()
    UI.layout(ui,640,240);ui.focus='entry';entry.editor=Edit.new(entry.value);Edit.select_all(entry.editor)
end,update=function(dt)
    UI.update(ui,dt,640,240)
    assert(entry.value=='Unchanged' and #entry.editor.undo==0)
    local segments=entry.composition_segments
    assert(#segments==6 and not entry.composition_segments_truncated)
    local kinds={};for _,p in ipairs(entry.text_layout.positions) do if p.kind then kinds[p.kind]=true end end
    for _,s in ipairs(segments) do assert(kinds[s.kind]) end
    sc.debug.watch('segments',segments);sc.debug.watch('value',entry.value)
end,draw=function()
    sc.rect(0,0,640,240,'#111C2E',true)
    sc.text('IME conversion segments',24,20,22,'#E6EDF7',true)
    UI.draw(ui)
    sc.text('raw: input   READ: target converted   done: converted',24,136,16,'#CBD5E1',true)
    sc.text('WAIT: target input   oops: error   lock: fixed',24,162,16,'#CBD5E1',true)
    sc.text('Preedit only; stored value remains Unchanged.',24,202,14,'#8CA4C4',true)
end}
''',encoding='utf-8')
kinds=['input','target_converted','converted','target_unconverted','error','fixed']
parts=['raw ','READ ','done ','WAIT ','oops ','lock']
segments=[];offset=1
for kind,part in zip(kinds,parts):
    segments.append(dict(start=offset,finish=offset+len(part),kind=kind));offset+=len(part)
replay=output/'segments.jsonl'
replay.write_text(json.dumps(dict(version=3))+'\n'+json.dumps(dict(frame=0,keys=[],gamepad=dict(connected=False),
    composition=''.join(parts),composition_edit=dict(cursor=20,start=5,finish=10),composition_segments=segments))+'\n')
command=[str(binary),str(project),'--frames','1','--capture-hidden','--capture',str(output/'segments.png'),
         '--mute','--replay',str(replay),'--save-dir',str(output/'saves')]
result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=20)
(output/'segments.log').write_text(result.stderr,encoding='utf-8')
assert result.returncode==0,result.stderr
data=json.loads(result.stdout)
assert data['input']['composition_segments']==segments
assert data['watches']['value']=='Unchanged'
(output/'segments.json').write_text(result.stdout,encoding='utf-8')
report=dict(engine_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),command=command,
            checks='passed',visual_review='pending',scope='recorded metadata; no physical IME acceptance')
(output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(output/'segments.png')
