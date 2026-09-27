"""Save metadata, exact arguments, return shapes and recoverable slot enumeration."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
contracts={f['name'].rsplit('.',1)[1]:f['contract'] for f in api['functions'] if f['name'].startswith('sc.save.')}
assert set(contracts)=={'read','list','write','load','delete','delete_async','read_chunk','write_chunks','write_async','write_chunks_async','read_chunks_async','result','status','retry','release'}
for name,contract in contracts.items():
    assert contract and all(p['required'] for p in contract['parameters'])
    expected=['load','init','update','draw','ui_update'] if name in ('read','list','status','result') else (
        ['load','init','update'] if name=='read_chunk' else ['update','ui_update'] if name in ('retry','release') else ['update'])
    assert contract['phases']==expected,(name,contract)
assert [p['name'] for p in contracts['write_chunks']['parameters']]==['slot','changes']
assert [r['name'] for r in contracts['read']['returns']]==['record','error']
assert [r['name'] for r in contracts['read_chunk']['returns']]==['state','error']
for name in ('write','load','write_chunks'):assert [r['name'] for r in contracts[name]['returns']]==['ok','error']
fields={name:{f['name']:f for f in api['types'][name]['fields']} for name in ('ScSaveRecord','ScSaveSlot')}
assert set(fields['ScSaveRecord'])=={'format','project','data_version','scene','state','frame','saved_at','chunk_count'}
assert set(fields['ScSaveSlot'])=={'slot','valid','scene','data_version','frame','saved_at','error'}
assert not fields['ScSaveRecord']['frame']['required'] and fields['ScSaveRecord']['state']['required']
assert next(field for field in api['types']['ScSaveStatus']['fields'] if field['name']=='operation')['type']=="'read'|'write'|'delete'"

with tempfile.TemporaryDirectory(prefix='shiny-save-contract-') as directory:
    project=Path(directory)
    (project/'project.lua').write_text('return {id="contracts",data_version=2}',encoding='utf-8')
    def run(source,frames=2,disk=False,check=False):
        (project/'main.lua').write_text(source,encoding='utf-8')
        args=[str(binary),str(project),'--check' if check else '--headless','--frames',str(frames)]
        if disk:args+=['--save-dir',str(project/'saves')]
        result=subprocess.run(args,capture_output=True,text=True,encoding='utf-8',timeout=20)
        assert result.returncode==0,result.stderr
        return json.loads(result.stdout)

    source='''local save=sc.save
local restored=sc.state.get('checkpoint')
local function mutations_forbidden()
    for _,name in ipairs({'write','load','delete','write_chunks'}) do
        if name=='write_chunks' then assert(not pcall(save[name],'slot',{}))
        else assert(not pcall(save[name],'slot')) end
    end
end
local function read_only()
    mutations_forbidden()
    assert(not pcall(save.read_chunk,'a','world/chunk:0:0'))
    local slots=save.list();assert(type(slots)=='table')
    local record,err=save.read('a')
    if record then assert(record.state.coins==42 and not err) else assert(type(err)=='string') end
end
return {init=function()
    -- Supply the exact argument count so phase checks, not arity, reject these calls.
    assert(not pcall(save.write,'a') and not pcall(save.load,'a') and not pcall(save.delete,'a'))
    assert(not pcall(save.write_chunks,'a',{}))
    if restored then assert(sc.state.get('coins')==42);sc.debug.watch('restored',true) end
end,update=function()
    if restored then sc.app.quit();return end
    for _,call in ipairs({function() save.list(1) end,function() save.read('a',true) end,
        function() save.delete('a',true) end,function() save.write('a',true) end,
        function() save.load('a',true) end,function() save.read_chunk('a') end,
        function() save.write_chunks('a') end}) do assert(not pcall(call)) end
    for _,name in ipairs({'read','write','load','delete'}) do
        for _,slot in ipairs({'','../escape','a'..string.char(0),'中文',string.rep('a',129),4,false}) do
            assert(not pcall(save[name],slot))
        end
    end
    local missing,err=save.read('absent');assert(missing==nil and type(err)=='string')
    sc.state.set('checkpoint',true);sc.state.set('coins',42)
    assert(select('#',save.write('b'))==1);assert(save.write('a'))
    local record=assert(save.read('a'))
    assert(record.format==3 and record.project=='contracts' and record.data_version==2)
    assert(record.frame==0 and record.saved_at>=0 and record.chunk_count==0 and record.chunks==nil)
    record.state.coins=100;assert(save.read('a').state.coins==42)
    local slots=save.list();assert(#slots==2 and slots[1].slot=='a' and slots[2].slot=='b')
    assert(slots[1].valid and slots[1].error==nil);slots[1].slot='changed';assert(save.list()[1].slot=='a')
    assert(save.delete('b') and save.delete('absent') and #save.list()==1)
    local ok,problem=save.write_chunks('a',{bad=true});assert(ok==nil and type(problem)=='string')
    ok,problem=save.read_chunk('absent','../bad');assert(ok==nil and type(problem)=='string')
    sc.state.set('coins',99);assert(select('#',save.load('a'))==1)
    assert(sc.state.get('coins')==99) -- Restoration is staged, not immediate.
end,draw=read_only,ui_update=read_only}
'''
    for disk in (False,True):assert run(source,disk=disk)['watches']['restored']
    run(source,check=True) # No update, no mutation; ordinary reads still work.

    saved=project/'saves/contracts'
    def record(**extra):
        return dict(format=3,project='contracts',data_version=2,scene='main.lua',state={'coins':7},**extra)
    def write(name,value):
        (saved/name).write_text(json.dumps(value),encoding='utf-8')
    write('recovered.json.bak',record())
    write('a.json.bak',record(frame=5,saved_at=8))
    write('invalid.json',dict(format=2))
    write('badmeta.json',record(frame='not an integer'))
    write('extra.json',record(custom='not public'))
    result=run('''return {init=function()
    local slots=sc.save.list();assert(#slots==5)
    for i,name in ipairs({'a','badmeta','extra','invalid','recovered'}) do assert(slots[i].slot==name) end
    assert(slots[1].valid and slots[5].valid)
    assert(not slots[2].valid and slots[2].error:find('metadata'))
    assert(not slots[4].valid and slots[4].error and not slots[4].scene)
    assert(sc.save.read('extra').custom==nil)
    local r=sc.save.read('recovered');assert(r.state.coins==7 and r.chunk_count==0 and r.frame==nil and r.saved_at==nil)
    local absent,err=sc.save.read_chunk('absent','world/chunk:0:0');assert(absent==nil and err==nil)
    sc.debug.watch('record',r);sc.debug.watch('slots',slots)
end}''',frames=0,disk=True)
    assert set(result['watches']['record'])==set(fields['ScSaveRecord'])-{'frame','saved_at'}
    for slot in result['watches']['slots']:
        assert set(slot)<=set(fields['ScSaveSlot'])
    # Malformed optional metadata makes the primary invalid and selects a complete backup.
    for bad in (-1,.5,9007199254740992,False):
        write('recovered.json',record(saved_at=bad))
        run("return {init=function() local r=assert(sc.save.read('recovered'));assert(r.saved_at==nil and r.state.coins==7) end}",frames=0,disk=True)
print('Save contracts: metadata, phases, detached data, staged load and backup-only enumeration passed')
