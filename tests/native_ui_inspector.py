"""Opt-in hidden UI captures: Chinese tooltip, scrollbar capture and Agent panel selection."""
import argparse
import hashlib
import json
from pathlib import Path
import queue
import shutil
import subprocess
import threading

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--case',action='append',choices=['tooltip','drag','panel','panel-tail'])
args=parser.parse_args()
binary=args.binary.resolve(); output=args.output.resolve(); output.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1]
project=output/'project'; project.mkdir()
shutil.copytree(root/'lua/shiny',project/'lib/shiny')
shutil.copyfile(root/'examples/wayfarer/assets/SourceHanSansSC-Regular.otf',project/'font.otf')
(project/'project.lua').write_text('''return {display={width=1152,height=648,vsync=false},
resources={ui={type="font",path="font.otf",size=16}}}''',encoding='utf-8')
(project/'main.lua').write_text('''local UI=require("shiny.ui")
local theme={}; for k,v in pairs(UI.theme) do theme[k]=v end
theme.font="ui";theme.font_size=12
local children={};for i=1,12 do children[i]={id="item-"..i,kind="button",h=26,text="月光草 "..i} end
local ui=UI.new({id="inventory",kind="overlay",padding=0,children={
    {id="catalog",kind="scroll",x=8,y=32,w=164,h=160,padding=4,gap=4,children=children},
    {id="help",kind="button",x=208,y=180,w=164,h=28,text="查看物品说明",tooltip_delay=0,tooltip_width=160,
     tooltip="月光草可用于村口药师的任务。采集后放入背包，滚动列表可查看全部物品。"}}},theme)
return {width=384,height=216,ambient=1,gravity=0,init=function()
    UI.layout(ui,384,216);ui.focus="help";sc.debug.ui("背包",ui)
end,update=function(dt)
    UI.update(ui,dt,384,216)
    sc.debug.watch("ui",(UI.inspect(ui)))
end,draw=function()
    sc.rect(0,0,384,216,"#111C2E",true)
    sc.text("背包 · 滚动与提示",8,7,14,"#E6EDF7",true,{font="ui"})
    UI.draw(ui)
end}
''',encoding='utf-8')
manifest={'engine_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'cases':[]}
for case in args.case or ['tooltip','drag','panel','panel-tail']:
    replay=output/(case+'.jsonl')
    inputs=[dict(version=3)]
    for frame in range(2):
        mouse=dict(x=168,y=40 if frame==0 else 150,inside=True,buttons=['left']) if case=='drag' else dict(x=380,y=5,inside=True,buttons=[])
        inputs.append(dict(frame=frame,keys=[],mouse=mouse,gamepad=dict(connected=False)))
    replay.write_text('\n'.join(json.dumps(value) for value in inputs)+'\n',encoding='utf-8')
    capture=output/(case+'.png'); snapshot=output/(case+'.json')
    command=[str(binary),str(project),'--debug-stdio','--mute','--frames','2','--capture-hidden',
             '--capture',str(capture),'--snapshot',str(snapshot),'--replay',str(replay),'--save-dir',str(output/'saves')]
    messages=queue.Queue(); transcript=[]
    with (output/(case+'.log')).open('w',encoding='utf-8') as log:
        process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True,encoding='utf-8',bufsize=1)
        def read():
            for line in process.stdout:
                try: messages.put(json.loads(line))
                except Exception as error: messages.put(error)
            messages.put(EOFError('debugger output closed'))
        reader=threading.Thread(target=read,daemon=True);reader.start()
        def receive():
            value=messages.get(timeout=10)
            if isinstance(value,Exception):raise value
            transcript.append(value);return value
        sequence=0
        def send(name,**fields):
            global sequence
            sequence+=1;process.stdin.write(json.dumps(dict(id=sequence,command=name,**fields))+'\n');process.stdin.flush()
            value=receive();assert value['id']==sequence,value
            return value
        try:
            assert receive()['event']=='ready'
            if case.startswith('panel'):
                for section in ['metrics','entities','resources','off']:
                    assert send('panel',section=section)['result']['section']==section
                configured=send('panel',section='ui',tree='背包',offset=12 if case=='panel-tail' else 0)['result']
                assert not send('panel',section='resources',offset=-1)['ok']
                assert send('panel')['result']==configured, 'invalid update changed panel state'
            assert send('step',count=1)['ok'];assert receive()['event']=='stopped'
            inspected=send('ui',tree='inventory')['result']['items']
            help_=next(row for row in inspected if row['id']=='help')
            scroll=next(row for row in inspected if row['id']=='catalog')
            assert scroll['scroll_max']==204,scroll
            if case=='drag': assert scroll['scroll_capture'] and not help_['tooltip_visible']
            else:
                assert help_['tooltip_visible'] and not help_['tooltip_truncated'],help_
                tip=help_['tooltip_rect'];assert tip['x']>=0 and tip['y']>=0 and tip['x']+tip['w']<=384 and tip['y']+tip['h']<=216
            assert send('step',count=1)['ok'];assert receive()['event']=='stopped'
            assert receive()['event']=='terminated';assert process.wait(timeout=10)==0
        finally:
            if process.poll() is None:process.kill();process.wait(timeout=10)
            process.stdin.close();reader.join(timeout=1);process.stdout.close()
    (output/(case+'-protocol.json')).write_text(json.dumps(transcript,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    data=json.loads(snapshot.read_text(encoding='utf-8'))
    if case=='drag':
        scroll=next(row for row in data['watches']['ui'] if row['id']=='catalog')
        assert scroll['scroll']==204 and scroll['scroll_capture'],scroll
    manifest['cases'].append(dict(case=case,command=command,checks='passed',visual_review='pending'))
    (output/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(case+': '+str(capture),flush=True)
