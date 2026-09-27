"""Opt-in hidden native screenshots of directional focus and slider ownership."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--case',action='append',choices=['grid','slider','slider-hold'])
args=parser.parse_args()
binary=args.binary.resolve(); output=args.output.resolve(); output.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1]; project=output/'project'; project.mkdir()
shutil.copytree(root/'lua/shiny',project/'lib/shiny')
shutil.copyfile(root/'examples/wayfarer/assets/SourceHanSansSC-Regular.otf',project/'font.otf')
(project/'project.lua').write_text('return {display={width=1152,height=648,vsync=false},resources={ui={type="font",path="font.otf",size=16}}}',encoding='utf-8')
(project/'main.lua').write_text('''local UI=require('shiny.ui')
local theme={};for k,v in pairs(UI.theme) do theme[k]=v end
theme.font='ui';theme.font_size=14
local function button(id,x,y,text) return {id=id,kind='button',x=x,y=y,w=104,h=32,text=text} end
-- Deliberately different author order: direction follows geometry, Tab follows order.
local ui=UI.new({id='menu',kind='overlay',padding=0,children={
    button('gear',16,44,'装备'),button('map',144,92,'地图'),
    button('quests',144,44,'任务'),button('items',16,92,'药剂'),
    {id='volume',kind='slider',x=16,y=154,w=232,h=18,value=.5,step=.1}}},theme)
return {width=384,height=216,ambient=1,gravity=0,init=function()
    UI.layout(ui,384,216);ui.focus='gear'
end,update=function(dt)
    UI.update(ui,dt,384,216)
    sc.debug.watch('focus',ui.focus);sc.debug.watch('volume',ui.nodes.volume.value)
end,draw=function()
    sc.rect(0,0,384,216,'#111C2E',true)
    sc.text('背包 · 方向导航',16,10,18,'#E6EDF7',true,{font='ui'})
    UI.draw(ui)
    sc.text('音量',16,132,12,'#8CA4C4',true,{font='ui'})
    sc.text('方向键移动焦点\\nTab 顺序切换\\n滑条左右调节',268,48,12,'#8CA4C4',true,{font='ui'})
    sc.text('当前焦点：'..ui.nodes[ui.focus].id,16,188,12,'#FFCB77',true,{font='ui'})
end}
''',encoding='utf-8')
report={'engine_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'cases':[]}
for case in args.case or ['grid','slider']:
    buttons=['dpad_right','dpad_down'] if case=='grid' else ['dpad_right','dpad_down',None,'dpad_down','dpad_right']
    if case=='slider-hold':buttons.extend(['dpad_right']*29)
    replay=output/(case+'.jsonl')
    rows=[dict(version=3)]+[dict(frame=i,keys=[],mouse=dict(x=380,y=210,inside=True,buttons=[]),
        gamepad=dict(connected=True,buttons=[button] if button else [])) for i,button in enumerate(buttons)]
    replay.write_text('\n'.join(json.dumps(row) for row in rows)+'\n',encoding='utf-8')
    command=[str(binary),str(project),'--frames',str(len(buttons)),'--capture-hidden',
        '--capture',str(output/(case+'.png')),'--mute','--replay',str(replay),'--save-dir',str(output/'saves')]
    result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=20)
    (output/(case+'.log')).write_text(result.stderr,encoding='utf-8')
    assert result.returncode==0,result.stderr
    data=json.loads(result.stdout);watch=data['watches']
    assert watch['focus']==('map' if case=='grid' else 'volume'),watch
    expected={'grid':.5,'slider':.6,'slider-hold':.8}[case]
    assert abs(watch['volume']-expected)<1e-6,watch
    (output/(case+'.json')).write_text(result.stdout,encoding='utf-8')
    report['cases'].append(dict(case=case,command=command,checks='passed',visual_review='pending'))
    (output/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(case+': '+str(output/(case+'.png')),flush=True)
