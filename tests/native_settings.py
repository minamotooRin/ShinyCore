"""Explicit native-window checks. Requires a desktop, Pillow and a graphics build."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from PIL import Image

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('output',type=Path)
args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
output=args.output.resolve()

with tempfile.TemporaryDirectory(prefix='shiny-native-settings-') as directory:
    project=Path(directory)
    (project/'project.lua').write_text('return {id="window-test",display={width=960,height=540},limits={particles=0}}',encoding='utf-8')
    data=project/'data/window-test/config'
    data.mkdir(parents=True)
    record=data/'settings.json'
    record.write_text(json.dumps({'format':1,'settings':{'width':960,'height':540}}),encoding='utf-8')
    (data/'settings.json.tmp').mkdir()  # Atomic replacement must fail after applying the window change.
    (project/'main.lua').write_text('''return {width=384,height=216,update=function()
    if sc.tick()==0 then
        local ok,error=sc.settings.apply({width=1280,height=720,scale="smooth"})
        assert(not ok and #error>0 and sc.settings.get().width==960)
        sc.debug.watch("rollback",true)
    end
end,draw=function() sc.rect(0,0,384,216,"#204060FF",true) end}''',encoding='utf-8')

    def run(name,frames):
        capture=output/(name+'.png')
        result=subprocess.run([str(args.binary.resolve()),str(project),'--save-dir',str(project/'data'),
            '--frames',str(frames),'--mute','--capture',str(capture)],capture_output=True,text=True,encoding='utf-8',timeout=30)
        (output/(name+'.log')).write_text(result.stderr,encoding='utf-8')
        if result.returncode: raise AssertionError(result.stderr)
        state=json.loads(result.stdout)
        (output/(name+'.json')).write_text(json.dumps(state,ensure_ascii=False,indent=2),encoding='utf-8')
        return state,Image.open(capture).convert('RGB')

    state,frame=run('settings-rollback',3)
    assert state['watches']['rollback'] and frame.size==(960,540)
    assert json.loads(record.read_text())['settings']['width']==960
    assert frame.getpixel((0,0))==(4,8,12) and frame.getpixel((480,270))==(32,64,96)
    (project/'main.lua').write_text('''return {width=384,height=216,update=function()
    local t=sc.tick()
    if t==0 or t==2 then
        assert(sc.settings.apply({mode="borderless",vsync=false},false))
        assert(sc.settings.get().mode=="borderless")
    elseif t==1 or t==3 then
        assert(sc.settings.apply({mode="windowed",width=1000,height=700,scale="integer"},false))
    end
    if t==4 then sc.debug.watch("mode_cycles",2) end
end,draw=function() sc.rect(0,0,384,216,"#204060FF",true) end}''',encoding='utf-8')
    state,frame=run('settings-modes',6)
    assert state['watches']['mode_cycles']==2 and frame.size==(1000,700)
    # Integer fit is exactly 2x, centered at (116,134), with unchanged pixels.
    for position in [(115,134),(116,133),(884,134),(116,566)]: assert frame.getpixel(position)==(4,8,12)
    for position in [(116,134),(883,565),(500,350)]: assert frame.getpixel(position)==(32,64,96)
    assert json.loads(record.read_text())['settings']['width']==960, 'session-only changes must not persist'
    print('native settings: disk-failure window rollback, two borderless cycles, actual framebuffer size and integer letterboxing passed')
