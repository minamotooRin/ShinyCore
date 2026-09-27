"""Opt-in hidden native IME rendering with recorded cursor/target positions (no live IME)."""
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
shutil.copyfile(root/'examples/wayfarer/assets/SourceHanSansSC-Regular.otf',project/'font.otf')
(project/'project.lua').write_text('return {display={width=1152,height=648,vsync=false},resources={ui={type="font",path="font.otf",size=16}}}')
(project/'main.lua').write_text('''local UI=require("shiny.ui")
local Edit=require("shiny.textedit")
local theme={};for k,v in pairs(UI.theme) do theme[k]=v end
theme.font="ui";theme.font_size=22
local entry={id="name",kind="input",x=40,y=70,w=304,h=44,value="旅人"}
local ui=UI.new({id="name-entry",kind="overlay",padding=0,children={entry}},theme)
return {width=384,height=216,ambient=1,init=function()
    UI.layout(ui,384,216);ui.focus="name";entry.editor=Edit.new(entry.value);Edit.select_all(entry.editor)
end,update=function(dt)
    UI.update(ui,dt,384,216)
    sc.debug.watch("value",entry.value)
    sc.debug.watch("cursor",entry.text_layout.preedit_cursor)
    sc.debug.watch("target",{entry.text_layout.target_first,entry.text_layout.target_last})
end,draw=function()
    sc.rect(0,0,384,216,"#111C2E",true)
    sc.text("请输入旅人姓名",40,25,20,"#E6EDF7",true,{font="ui"})
    UI.draw(ui)
    sc.text("组合文本尚未提交 · 高亮当前转换片段",40,136,12,"#8CA4C4",true,{font="ui"})
end}
''',encoding='utf-8')
report={'engine_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'cases':[]}
for case,cursor,start,finish in [('middle',4,4,7),('start',1,1,4)]:
    replay=output/(case+'.jsonl')
    replay.write_text(json.dumps(dict(version=3))+'\n'+json.dumps(dict(frame=0,keys=[],gamepad=dict(connected=False),
        composition='月光草',composition_edit=dict(cursor=cursor,start=start,finish=finish)))+'\n',encoding='utf-8')
    command=[str(binary),str(project),'--frames','1','--capture-hidden','--capture',str(output/(case+'.png')),
             '--mute','--replay',str(replay),'--save-dir',str(output/'saves')]
    result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=20)
    (output/(case+'.log')).write_text(result.stderr,encoding='utf-8')
    assert result.returncode==0,result.stderr
    data=json.loads(result.stdout);watch=data['watches']
    assert watch['value']=='旅人' and watch['cursor']==cursor and watch['target']==[start,finish],watch
    (output/(case+'.json')).write_text(result.stdout,encoding='utf-8')
    report['cases'].append(dict(case=case,command=command,checks='passed',visual_review='pending'))
    (output/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(case+': '+str(output/(case+'.png')),flush=True)
