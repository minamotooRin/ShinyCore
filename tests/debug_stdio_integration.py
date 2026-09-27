"""Bounded stdio protocol, frame stepping, inspection and disconnect retention."""
import json
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time

binary=Path(sys.argv[1]).resolve()
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
if not api['modules']['devtools']:
    rejected=subprocess.run([str(binary),'--debug-stdio','--headless'],capture_output=True,text=True)
    assert rejected.returncode and 'SHINY_DEVTOOLS' in rejected.stderr
    print('debug stdio disabled-build rejection passed')
    raise SystemExit(0)

with tempfile.TemporaryDirectory(prefix='shiny-debug-') as directory:
    project=Path(directory)
    (project/'main.lua').write_text('''return {init=function()
sc.spawn{tag="agent",x=10}; sc.state.set("progress",{count=0,items={"first","second"}})
print("game log on stderr")
end,update=function()
local state=sc.state.get("progress"); state.count=state.count+1; sc.state.set("progress",state)
sc.debug.watch("progress",state.count)
end}''')
    process=subprocess.Popen([str(binary),str(project),'--headless','--debug-stdio','--frames','100'],
        stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',bufsize=1)
    messages=queue.Queue()
    def receive():
        for line in process.stdout:
            try: messages.put(json.loads(line))
            except Exception as error: messages.put(error)
    reader=threading.Thread(target=receive,daemon=True);reader.start()
    def message():
        result=messages.get(timeout=10)
        if isinstance(result,Exception): raise result
        return result
    def command(identifier,cmd,**fields):
        process.stdin.write(json.dumps({'id':identifier,'command':cmd,**fields})+'\n');process.stdin.flush()
        result=message(); assert result['id']==identifier,result
        return result
    try:
        ready=message(); assert ready['event']=='ready' and ready['paused'] and ready['protocol']==1
        assert command(1,'status')['frame']==0
        assert not command('no-panel','panel',section='ui')['ok']
        assert command(2,'step',count=3)['pending_steps']==3
        stopped=message(); assert stopped['event']=='stopped' and stopped['frame']==3
        result=command(3,'watches',path=['progress']); assert result['result']['value']==3
        result=command(4,'state',path=['progress','items'],offset=1,limit=1)
        assert result['result']['total']==2 and result['result']['items']==[{'key':1,'value':'second'}]
        result=command(5,'entities',limit=1); assert result['result']['items'][0]['tag']=='agent'
        assert not command(6,'state',path=['missing'])['ok']
        assert not command(7,'evaluate',expression='os.execute("bad")')['ok']
        process.stdin.write('{broken\n');process.stdin.flush();assert not message()['ok']
        assert not command(8,'entities',limit=129)['ok']
        assert command(9,'status')['frame']==3
        process.stdin.write('x'*16385+'\n');process.stdin.flush()
        oversized=message();assert not oversized['ok'] and 'exceeds' in oversized['error']
        assert command('after-limit','status')['frame']==3
        process.stdin.write(json.dumps({'id':'go','command':'continue'})+'\n'+json.dumps({'id':'stop','command':'pause'})+'\n')
        process.stdin.flush()
        assert message()['paused'] is False
        assert message()['paused'] is True
        assert command(10,'quit')['ok']
        assert message()['event']=='terminated'
        assert process.wait(timeout=10)==0
        assert 'game log on stderr' in process.stderr.read()
    finally:
        if process.poll() is None: process.kill();process.wait(timeout=10)
        process.stdin.close();process.stdout.close();process.stderr.close();reader.join(timeout=1)
    # Closing the controller input never implicitly resumes the paused game.
    process=subprocess.Popen([str(binary),str(project),'--headless','--debug-stdio','--frames','1'],
        stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
    try:
        assert json.loads(process.stdout.readline())['event']=='ready'
        process.stdin.close()
        disconnected=json.loads(process.stdout.readline())
        assert disconnected['event']=='disconnected' and disconnected['paused'] and disconnected['frame']==0
        time.sleep(.05);assert process.poll() is None
    finally:
        process.kill();process.wait(timeout=10);process.stdout.close();process.stderr.close()
print('debug stdio: stepping, paged inspection, invalid requests, stderr and EOF pause passed')
