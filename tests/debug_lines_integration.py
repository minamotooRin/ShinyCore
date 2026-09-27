"""Pause inside real Lua frames without replaying calls or evaluating inspectors."""
import json
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading

binary=Path(sys.argv[1]).resolve()
if not json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))['modules']['devtools']:
    print('debug lines: devtools disabled; not run');raise SystemExit(0)
with tempfile.TemporaryDirectory(prefix='shiny-debug-lines-') as directory:
    project=Path(directory)
    (project/'main.lua').write_text('''local function inner(value)
    local doubled=value*2
    return doubled
end
return {init=function()
    sc.state.set("visits",0)
end,update=function()
    local before=sc.state.get("visits")
    local value=inner(before+1)
    sc.state.set("visits",value)
    sc.debug.watch("visits",value)
end}
''')
    process=subprocess.Popen([str(binary),str(project),'--headless','--debug-stdio','--frames','10'],
        stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',bufsize=1)
    messages=queue.Queue()
    def read():
        for line in process.stdout:
            try: messages.put(json.loads(line))
            except Exception as error: messages.put(error)
    reader=threading.Thread(target=read,daemon=True);reader.start()
    def message():
        result=messages.get(timeout=10)
        if isinstance(result,Exception): raise result
        return result
    identifier=0
    def command(name,**fields):
        global identifier
        identifier+=1
        process.stdin.write(json.dumps({'id':identifier,'command':name,**fields})+'\n');process.stdin.flush()
        response=message();assert response.get('id')==identifier,response
        return response
    def stopped(line):
        result=message();assert result['event']=='breakpoint' and result['file']=='main.lua' and result['line']==line,result
    def locals_():
        return {v['name']:v['value'] for v in command('locals')['result']['items']}
    try:
        assert 'breakpoints' in message()['commands']
        assert not command('locals')['ok']
        assert command('breakpoints',file='main.lua',lines=[9])['ok']
        assert command('continue')['ok'];stopped(9)
        frames=command('stack')['result']['items'];assert frames[0]['line']==9 and frames[0]['file']=='main.lua'
        assert locals_()['before']=={'type':'integer','value':'0'} or locals_()['before']==0
        assert command('step_in')['ok'];stopped(2)
        assert locals_()['value']=={'type':'integer','value':'1'} or locals_()['value']==1
        assert command('step_over')['ok'];stopped(3)
        assert locals_()['doubled']=={'type':'integer','value':'2'} or locals_()['doubled']==2
        assert command('step_out')['ok'];stopped(10)
        assert locals_()['value']=={'type':'integer','value':'2'} or locals_()['value']==2
        assert not command('step')['ok']
        assert command('breakpoints',file='main.lua',lines=[])['ok']
        assert command('step_over')['ok'];stopped(11)
        assert command('state',path=['visits'])['result']['value']==2
        assert command('step_over')['ok'];stopped(12)
        assert command('step_out')['ok'];stopped(8)
        assert command('status')['frame']==1
        assert command('state',path=['visits'])['result']['value']==2
        assert command('quit')['ok']
        assert message()['event']=='terminated'
        assert process.wait(timeout=10)==0,process.stderr.read()
    finally:
        if process.poll() is None: process.kill();process.wait(timeout=10)
        process.stdin.close();process.stdout.close();process.stderr.close();reader.join(timeout=1)
    # A resume at a breakpoint must not disable the instruction budget.
    (project/'main.lua').write_text('''return {update=function()
local i=0
while true do i=i+1 end
end}''')
    error_log=project/'budget-errors.txt'
    error_output=error_log.open('w',encoding='utf-8')
    process=subprocess.Popen([str(binary),str(project),'--headless','--debug-stdio'],
        stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=error_output,text=True,encoding='utf-8')
    try:
        assert json.loads(process.stdout.readline())['event']=='ready'
        process.stdin.write('{"id":1,"command":"breakpoints","file":"main.lua","lines":[3]}\n{"id":2,"command":"continue"}\n');process.stdin.flush()
        assert json.loads(process.stdout.readline())['ok']
        assert json.loads(process.stdout.readline())['ok']
        assert json.loads(process.stdout.readline())['event']=='breakpoint'
        process.stdin.write('{"id":3,"command":"breakpoints","file":"main.lua","lines":[]}\n{"id":4,"command":"continue"}\n');process.stdin.flush()
        assert json.loads(process.stdout.readline())['ok']
        assert json.loads(process.stdout.readline())['ok']
        assert process.wait(timeout=30)!=0
        errors=error_log.read_text(encoding='utf-8')
        assert 'instruction budget exceeded' in errors,errors
        assert 'AddressSanitizer' not in errors and 'runtime error:' not in errors,errors
    finally:
        if process.poll() is None: process.kill();process.wait(timeout=10)
        process.stdin.close();process.stdout.close();error_output.close()
print('debug lines: stack, raw locals, source steps, no repeated calls and budget retention passed')
