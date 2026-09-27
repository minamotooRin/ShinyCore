"""Inspect real paused local tables without expressions, metamethods or retained VM refs."""
import json
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading

binary = Path(sys.argv[1]).resolve()
source = '''local function forbidden() error("inspector invoked metamethod") end
return {update=function()
    local data={inventory={{name="月光草",count=3}}, [true]="boolean", [1.5]="fraction",
        [9223372036854775807]="maximum", [-9223372036854775807-1]="minimum"}
    data.self=data
    data["zero"..string.char(0).."key"]="nul"
    data.long=string.rep("草",200)
    data.empty={}
    data.many={}; for i=1,300 do data.many[i]=i*2 end
    data.huge={}; for i=1,65540 do data.huge[i]=i end
    setmetatable(data,{__index=forbidden,__pairs=forbidden,__len=forbidden,__tostring=forbidden})
    local marker=17
    sc.state.set("marker",marker) -- BREAK
    sc.debug.watch("count",data.inventory[1].count) -- AFTER
end}
'''
break_line = next(i for i, line in enumerate(source.splitlines(), 1) if '-- BREAK' in line)
with tempfile.TemporaryDirectory(prefix='shiny-debug-locals-') as directory:
    project = Path(directory)
    (project / 'main.lua').write_text(source, encoding='utf-8')
    process = subprocess.Popen([str(binary), str(project), '--headless', '--debug-stdio', '--frames', '2'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding='utf-8', bufsize=1)
    messages = queue.Queue()
    def read():
        for line in process.stdout:
            try: messages.put(json.loads(line))
            except Exception as error: messages.put(error)
    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    def message():
        value = messages.get(timeout=10)
        if isinstance(value, Exception): raise value
        return value
    identifier = 0
    def command(name, **fields):
        global identifier
        identifier += 1
        process.stdin.write(json.dumps(dict(id=identifier, command=name, **fields)) + '\n')
        process.stdin.flush()
        response = message()
        assert response.get('id') == identifier, response
        return response
    def inspect(path=None, **fields):
        response = command('locals', variable=variable, path=path or [], **fields)
        assert response['ok'], response
        return response['result']
    def integer(value): return dict(type='integer', value=str(value))
    try:
        assert message()['inspection'] == dict(local_tables=True, path_limit=16, entry_budget=65536)
        assert command('breakpoints', file='main.lua', lines=[break_line])['ok']
        assert command('continue')['ok']
        assert message()['line'] == break_line
        variables = command('locals')['result']['items']
        variable = next(row['index'] for row in variables if row['name'] == 'data')
        assert inspect(['inventory', 1, 'name'])['value'] == '月光草'
        assert inspect(['self', 'inventory', 1, 'count'])['value'] == integer(3)
        assert inspect([True])['value'] == 'boolean'
        assert inspect([1.5])['value'] == 'fraction'
        assert inspect([integer(9223372036854775807)])['value'] == 'maximum'
        assert inspect([integer(-9223372036854775808)])['value'] == 'minimum'
        assert inspect(['zero\0key'])['value'] == 'nul'
        preview = inspect(['long'])['value']
        assert preview == dict(type='string', bytes=600, preview='草'*170), preview
        assert inspect(['empty']) == dict(type='table', items=[], offset=0, next_offset=None)
        rows, offset = [], 0
        while offset is not None:
            page = inspect(['many'], offset=offset, limit=127)
            rows += page['items']
            offset = page['next_offset']
        assert len(rows) == 300
        assert {int(row['key']['value']): int(row['value']['value']) for row in rows} == {i:i*2 for i in range(1,301)}
        assert inspect(['many'], offset=400)['items'] == []
        root = inspect()
        assert next(row for row in root['items'] if row['key'] == 'self')['value'] == {'type':'table'}
        for path in [['missing'], ['inventory',0], ['inventory',1,'name','child'],
                     [None], [dict(type='integer',value='9223372036854775808')],
                     [dict(type='integer',value='1x')], [4503599627370496], ['self']*17]:
            assert not command('locals', variable=variable, path=path)['ok'], path
        for fields in [dict(variable=-1), dict(variable=1023), dict(variable=variable,limit=0),
                       dict(variable=variable,offset=65536), dict(path=['data'])]:
            assert not command('locals', **fields)['ok'], fields
        exhausted = command('locals', variable=variable, path=['huge'], offset=65535, limit=128)
        assert not exhausted['ok'] and 'scan budget' in exhausted['error'], exhausted
        # Each failed inspection must restore the Lua stack and leave the paused call intact.
        assert inspect(['inventory',1,'count'])['value'] == integer(3)
        assert command('locals')['result']['items'] == variables
        assert command('step_over')['ok']
        assert message()['line'] == break_line+1
        assert command('state',path=['marker'])['result']['value'] == 17
        assert inspect(['inventory',1,'count'])['value'] == integer(3)
        assert command('quit')['ok']
        assert message()['event'] == 'terminated'
        assert process.wait(timeout=10) == 0
        errors = process.stderr.read()
        assert 'AddressSanitizer' not in errors and 'runtime error:' not in errors, errors
    finally:
        if process.poll() is None: process.kill(); process.wait(timeout=10)
        process.stdin.close(); process.stdout.close(); process.stderr.close(); reader.join(timeout=1)
print('debug locals: raw paths, lossless keys, pagination, cycles, scan limits and resume passed')
