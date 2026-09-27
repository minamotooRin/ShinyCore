"""Bounded real-process loading breakpoints and candidate lifetime checks."""
import json
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading

binary = Path(sys.argv[1]).resolve()

class Session:
    def __init__(self, project, *options):
        self.process = subprocess.Popen([str(binary), str(project), '--headless', '--debug-stdio',
            '--frames', '2', *options], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, encoding='utf-8', bufsize=1)
        self.messages = queue.Queue()
        self.identifier = 0
        self.errors = ''
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()
        self.ready = self.message()
        assert self.ready['loading_breakpoints'], self.ready
    def read(self):
        for line in self.process.stdout:
            try: self.messages.put(json.loads(line))
            except Exception as error: self.messages.put(error)
        self.errors = self.process.stderr.read()
        self.messages.put(RuntimeError('debugger output closed: '+self.errors))
    def message(self):
        value = self.messages.get(timeout=10)
        if isinstance(value, Exception): raise value
        return value
    def command(self, name, **fields):
        self.identifier += 1
        self.process.stdin.write(json.dumps(dict(id=self.identifier, command=name, **fields))+'\n')
        self.process.stdin.flush()
        response = self.message()
        assert response.get('id') == self.identifier, response
        assert response['ok'], response
        return response
    def stop(self, file, line, candidate=False, phase=0):
        event = self.message()
        assert event['event'] == 'breakpoint' and event['file'] == file and event['line'] == line, event
        assert event['candidate'] == candidate and event['phase'] == phase, event
        return event
    def quit(self):
        self.command('quit')
        assert self.message()['event'] == 'terminated'
        assert self.process.wait(timeout=10) == 0
        self.reader.join(timeout=1)
        errors = self.errors
        assert 'AddressSanitizer' not in errors and 'runtime error:' not in errors, errors
    def close(self):
        if self.process.poll() is None: self.process.kill(); self.process.wait(timeout=10)
        self.reader.join(timeout=1)
        self.process.stdin.close(); self.process.stdout.close(); self.process.stderr.close()

with tempfile.TemporaryDirectory(prefix='shiny-debug-load-') as directory:
    project = Path(directory)
    (project/'project.lua').write_text('local entry="main.lua"\nreturn {entry=entry,rooms={"next.lua"}}\n')
    (project/'main.lua').write_text('''local helper=require("helper")
return {init=function()
    local count=helper.count
    sc.state.set("count",count)
end,update=function()
    sc.scene("next.lua")
end,draw=function()
    sc.text("initial",1,1,10,"#ffffff")
end}
''')
    (project/'helper.lua').write_text('local value=21\nreturn {count=value}\n')
    (project/'next.lua').write_text('''local loaded=true
return {init=function()
    local before=sc.state.get("count")
    sc.state.set("count",before+1)
    assert(before==21)
end,update=function()
    sc.debug.watch("count",sc.state.get("count"))
end}
''')
    session = Session(project, '--debug-load')
    try:
        assert session.ready['break_on_entry']
        session.stop('project.lua',1)
        assert session.command('ui')['result']['total'] == 0
        session.command('breakpoints',file='helper.lua',lines=[2])
        session.command('breakpoints',file='main.lua',lines=[4,8])
        session.command('breakpoints',file='next.lua',lines=[1,5,7])
        session.command('continue'); session.stop('helper.lua',2)
        variables = session.command('locals')['result']['items']
        assert next(row for row in variables if row['name']=='value')['value'] == {'type':'integer','value':'21'}
        session.command('continue'); session.stop('main.lua',4)
        session.command('step_over'); session.stop('main.lua',5)
        assert session.command('state',path=['count'])['result']['value'] == 21
        session.command('continue'); session.stop('main.lua',8,phase=2)
        session.command('continue'); event = session.stop('next.lua',1,candidate=True)
        assert event['frame'] == 1
        assert session.command('state',path=['count'])['result']['value'] == 21
        session.command('continue'); session.stop('next.lua',5,candidate=True)
        assert session.command('state',path=['count'])['result']['value'] == 22
        session.command('continue'); session.stop('next.lua',7,phase=1)
        assert session.command('status')['room'] == 'next.lua'
        session.quit()
    finally: session.close()

    # Existing post-init mode also attaches configured breakpoints before candidate load.
    session = Session(project)
    try:
        assert not session.ready['break_on_entry']
        session.command('breakpoints',file='next.lua',lines=[5])
        session.command('continue'); session.stop('next.lua',5,candidate=True)
        session.quit()  # Quit must unwind the candidate and exit successfully, without committing it.
    finally: session.close()

    session = Session(project,'--debug-load')
    try:
        session.stop('project.lua',1)
        session.quit()  # No active room/window exists yet.
    finally: session.close()

    (project/'project.lua').unlink()
    (project/'main.lua').write_text('local count=0\nwhile true do count=count+1 end\n')
    session = Session(project,'--debug-load')
    try:
        session.stop('main.lua',1)
        session.command('continue')
        assert session.process.wait(timeout=10) != 0
        session.reader.join(timeout=1)
        errors = session.errors
        assert 'instruction budget exceeded' in errors, errors
        assert 'AddressSanitizer' not in errors and 'runtime error:' not in errors, errors
    finally: session.close()

rejected = subprocess.run([str(binary),'--debug-load'], capture_output=True, text=True)
assert rejected.returncode and 'requires --debug-stdio' in rejected.stderr
print('debug load: project, require, init/draw, candidate stops/commit, quit and load budget passed')
