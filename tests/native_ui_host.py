"""Opt-in hidden UI-first host parity and one-frame live scheduling check."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
binary=args.binary.resolve();output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1]
sys.argv=['input_integration.py',str(binary),str(root)]
spec=importlib.util.spec_from_file_location('input_tests',root/'tests/input_integration.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
case=module.InputTests('test_host_ui_actions_precede_gameplay');case.setUp()
report=[]
try:
    original=case.invoke
    def capture(*arguments,**kwargs):
        baseline=original(*arguments,**kwargs)
        project=output/'project';shutil.copytree(case.path,project)
        shutil.copyfile(root/'examples/wayfarer/assets/SourceHanSansSC-Regular.otf',project/'font.otf')
        (project/'project.lua').write_text('return {display={width=1152,height=648,vsync=false},resources={ui={type="font",path="font.otf",size=16}}}',encoding='utf-8')
        source=(project/'main.lua').read_text(encoding='utf-8')
        source=source.replace('draw=function() UI.draw(ui) end', '''draw=function()
            sc.rect(0,0,384,216,'#111C2E',true)
            sc.text('界面输入优先',22,24,22,'#E6EDF7',true,{font='ui'})
            sc.text('菜单确认：'..clicks..' 次',22,74,16,'#FFCB77',true,{font='ui'})
            sc.text('玩法跳跃：'..jumps..' 次',22,108,16,'#8FDCC8',true,{font='ui'})
            sc.text('关闭菜单的确认键没有触发跳跃',22,166,13,'#AABBD2',true,{font='ui'})
            UI.draw(ui)
        end''')
        (project/'main.lua').write_text(source,encoding='utf-8')
        command=[str(binary),str(project),'--frames','7','--mute','--capture-hidden',
                 '--capture',str(output/'ui-actions.png'),'--replay',str(project/'input.jsonl'),
                 '--save-dir',str(output/'saves')]
        result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=20)
        (output/'replay.log').write_text(result.stderr,encoding='utf-8')
        assert result.returncode==0,result.stderr
        snapshot=json.loads(result.stdout)
        assert snapshot['watches']==baseline['watches']
        (output/'snapshot.json').write_text(result.stdout,encoding='utf-8')
        report.append(dict(case='replay',command=command,capture='ui-actions.png',checks='passed',visual_review='pending'))
        return snapshot
    case.invoke=capture
    case.test_host_ui_actions_precede_gameplay()
finally:
    case.doCleanups()

# Exercise the live-sampling branch without depending on physical input or changing devices.
project=output/'live';project.mkdir()
(project/'main.lua').write_text('''local seen=false
return {ui_update=function() seen=true;sc.input.focus_text(20,20) end,
update=function() assert(seen,'live UI must precede gameplay');sc.debug.watch('ui_first',true) end,
draw=function() sc.rect(0,0,384,216,'#111C2E',true);sc.text('UI > UPDATE > DRAW',24,48,20,'#8FDCC8',true) end}
''',encoding='utf-8')
command=[str(binary),str(project),'--frames','1','--mute','--capture-hidden',
         '--capture',str(output/'live-order.png'),'--save-dir',str(output/'live-saves')]
result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=20)
(output/'live.log').write_text(result.stderr,encoding='utf-8')
assert result.returncode==0,result.stderr
assert json.loads(result.stdout)['watches']['ui_first'] is True
report.append(dict(case='live',command=command,capture='live-order.png',checks='passed',visual_review='pending'))
record=dict(engine_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),cases=report)
(output/'manifest.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(output)
