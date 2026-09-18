"""Replay the real settings menu; optionally verify the native framebuffer."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('root', type=Path)
parser.add_argument('--native', action='store_true')
parser.add_argument('--capture', type=Path)
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix='shiny-settings-') as directory:
    project = Path(directory)
    shutil.copytree(args.root / 'lua/shiny', project / 'lib/shiny')
    (project / 'project.lua').write_text('return {id="settings-menu",limits={particles=0}}', encoding='utf-8')
    (project / 'main.lua').write_text('''
local Shell=require("shiny.shell")
local Input=require("shiny.input")
local UI=require("shiny.ui")
local shell
return {width=384,height=216,init=function() shell=Shell.new("SETTINGS TEST","Replay controls the menu") end,
update=function(dt)
    Shell.update(shell,dt)
    local t=sc.tick()
    if t==5 then assert(shell.settings.draft.width==1280 and sc.settings.get().width==1152) end
    if t==7 then assert(not shell.settings and shell.mode=="title" and sc.settings.get().width==1152) end
    if t==37 then
        local settings=sc.settings.get()
        assert(settings.width==1280 and settings.height==720 and settings.scale=="smooth" and not settings.vsync)
        assert(math.abs(settings.volume.master-.95)<.00001 and math.abs(settings.volume.ui-.95)<.00001)
        assert(settings.volume.music==1 and settings.volume.sfx==1)
        local input=Input.new({jump={{key="space"}}},"player")
        Input.bind(input,"jump",{{key="j"}}); assert(Input.save(input))
        assert(Input.new({},"player").bindings.jump[1].key=="j")
    end
    if t==39 then assert(not shell.settings and shell.mode=="title" and sc.app.paused()) end
    if t==41 then
        assert(shell.mode=="game" and not sc.app.paused())
        -- Hidden and disabled controls must lose focus before Enter dispatch.
        local count=0
        local ui=UI.new({id="root",children={{id="button",kind="button",on_click=function() count=count+1 end}}})
        UI.layout(ui,384,216); ui.focus="button"; UI.set(ui,"button",{visible=false})
        UI.update(ui,dt,384,216); assert(not ui.focus and count==0)
        UI.set(ui,"button",{visible=true,disabled=true}); ui.focus="button"
        UI.update(ui,dt,384,216); assert(not ui.focus and count==0)
        sc.debug.watch("settings_verified",true)
    end
end,draw=function()
    sc.rect(0,0,384,216,"#102438FF",true)
    Shell.draw(shell)
end}
''', encoding='utf-8')
    actions = {0:'tab',2:'enter',4:'enter',6:'escape',8:'enter',10:'enter',
               12:'tab',14:'tab',16:'enter',18:'tab',20:'enter',22:'tab',24:'left',
               26:'tab',28:'tab',30:'tab',32:'left',34:'tab',36:'enter',38:'escape',40:'escape',41:'enter'}
    events = [{'version':3}]
    events.extend({'frame':frame,'keys':[actions[frame]] if frame in actions else [],
                   'gamepad':{'connected':False}} for frame in range(42))
    replay = project / 'replay.jsonl'
    replay.write_text('\n'.join(json.dumps(event) for event in events)+'\n', encoding='utf-8')
    capture = args.capture.resolve() if args.capture else project / 'settings.png'
    command = [str(args.binary.resolve()),str(project),'--frames','42','--replay',str(replay),
               '--save-dir',str(project/'data')]
    if args.native:
        # Capture while the complete menu is still visible after Apply.
        command[command.index('42')] = '38'
        command += ['--capture',str(capture)]
    else:
        command += ['--headless']
    result = subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=30)
    if result.returncode: raise AssertionError(result.stderr)
    snapshot = json.loads(result.stdout)
    if not args.native: assert snapshot['watches']['settings_verified']
    record = json.loads((project/'data/settings-menu/config/settings.json').read_text(encoding='utf-8'))
    assert record['settings']['width']==1280 and record['settings']['bindings']['player']['jump'][0]['key']=='j'
    if args.native:
        assert struct.unpack('>II',capture.read_bytes()[16:24])==(1280,720), 'capture must match applied window resolution'
    print('settings: draft/discard/apply, four gains and binding persistence passed'+
          (' (native framebuffer)' if args.native else '; hidden/disabled focus passed (headless)'))
