"""Capture one bounded native multiline page-selection state; requires visual review."""
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
binary=args.binary.resolve(); output=args.output.resolve(); output.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1]; project=output/'project'; project.mkdir()
shutil.copytree(root/'lua/shiny',project/'lib/shiny')
shutil.copyfile(root/'examples/wayfarer/assets/SourceHanSansSC-Regular.otf',project/'font.otf')
(project/'project.lua').write_text('return {display={width=1152,height=648,vsync=false},resources={ui={type="font",path="font.otf",size=16}}}',encoding='utf-8')
value='行旅日记\n第一日：走过苔石小径。\n第二日：在村口遇见药师。\n第三日：沿着月光寻找草药。\n第四日：清理堵住道路的石头。\n第五日：带着草药返回村庄。\n第六日：换上轻便的靴子。\n明天继续出发。'
source=r'''local UI=require('shiny.ui')
local Edit=require('shiny.textedit')
local theme={};for k,v in pairs(UI.theme) do theme[k]=v end
theme.font='ui';theme.font_size=16
local node={id='journal',kind='input',multiline=true,x=22,y=54,w=340,h=92,value=VALUE}
local ui=UI.new({id='journal-editor',kind='overlay',padding=0,children={node}},theme)
return {width=384,height=216,ambient=1,init=function()
    UI.layout(ui,384,216);ui.focus='journal'
    node.editor=Edit.new(node.value);node.editor.cursor=1;node.editor.anchor=1
end,update=function(dt)
    UI.update(ui,dt,384,216)
    sc.debug.watch('cursor',node.editor.cursor)
    sc.debug.watch('selected',Edit.selected(node.editor))
    sc.debug.watch('scroll_y',node.text_scroll_y)
    sc.debug.watch('value',node.value)
end,draw=function()
    sc.rect(0,0,384,216,'#111C2E',true)
    sc.text('行旅日记 · 多行编辑',22,18,20,'#E6EDF7',true,{font='ui'})
    UI.draw(ui)
    sc.text('Shift + Page Down：扩选一页',22,162,13,'#FFCB77',true,{font='ui'})
    sc.text('方向键移动 · Ctrl + Home / End 到全文首尾',22,188,11,'#8CA4C4',true,{font='ui'})
end}
'''
(project/'main.lua').write_text(source.replace('VALUE',json.dumps(value,ensure_ascii=False)),encoding='utf-8')
replay=output/'page-selection.jsonl'
replay.write_text('\n'.join(json.dumps(row) for row in [dict(version=3),dict(frame=0,
    keys=['left_shift','page_down'],gamepad=dict(connected=False))])+'\n',encoding='utf-8')
capture=output/'page-selection.png'
command=[str(binary),str(project),'--frames','1','--capture-hidden','--capture',str(capture),
         '--mute','--replay',str(replay),'--save-dir',str(output/'saves')]
result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=20)
(output/'run.log').write_text(result.stderr,encoding='utf-8')
assert result.returncode==0,result.stderr
snapshot=json.loads(result.stdout); watch=snapshot['watches']; selected=value[:value.index('第五日')]
assert watch['value']==value and watch['selected']==selected and watch['cursor']==len(selected.encode('utf-8'))+1,watch
assert watch['scroll_y']>0,watch
(output/'snapshot.json').write_text(result.stdout,encoding='utf-8')
report=dict(engine_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),command=command,
            capture=capture.name,checks='passed',visual_review='pending')
(output/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(capture)
