"""Async slot deletion keeps UI recovery and excludes overlapping save operations."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='shiny-save-delete-') as folder:
    project=Path(folder)
    (project/'project.lua').write_text('return {id="delete_test",data_version=1}',encoding='utf-8')
    save_root=project/'saves'

    def run(source,frames=3,disk=True):
        (project/'main.lua').write_text(source,encoding='utf-8')
        command=[str(binary),str(project),'--headless','--frames',str(frames)]
        if disk:command+=['--save-dir',str(save_root)]
        result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=15)
        assert result.returncode==0,result.stderr
        return json.loads(result.stdout)

    result=run('''local request
return {init=function() assert(not pcall(sc.save.delete_async,'slot')) end,
update=function()
    if sc.tick()==0 then
        assert(sc.save.write_chunks('slot',{piece={value=3}}))
        request=assert(sc.save.delete_async('slot'))
        assert(sc.save.status(request).operation=='delete')
        assert(not pcall(sc.save.read,'slot') and not pcall(sc.save.load,'slot'))
        assert(not pcall(sc.save.result,request))
    elseif sc.tick()==1 then
        assert(sc.save.status(request).status=='complete')
        assert(not pcall(sc.save.result,request))
        sc.save.release(request)
        assert(sc.save.read('slot')==nil)
        request=assert(sc.save.delete_async('slot'))
    elseif sc.tick()==2 then
        assert(sc.save.status(request).status=='complete')
        sc.save.release(request)
        sc.debug.watch('deleted',true)
    end
end}''')
    assert result['watches']['deleted']
    slot=save_root/'delete_test/slot.json'
    assert not slot.exists() and not Path(str(slot)+'.bak').exists()
    assert not Path(str(slot)+'.chunks').exists()

    blocked=slot.with_name('blocked.json')
    blocked.mkdir()
    (blocked/'keep').write_text('owned by fixture',encoding='utf-8')
    result=run('''local request,failed
return {update=function()
    if sc.tick()==0 then request=assert(sc.save.delete_async('blocked')) end
    sc.debug.watch('failed',failed==true)
end,ui_update=function()
    if request and sc.save.status(request).status=='failed' then
        assert(sc.save.status(request).operation=='delete')
        sc.save.release(request);request=nil;failed=true
    end
end}''',frames=2)
    assert result['watches']['failed'] and (blocked/'keep').read_text(encoding='utf-8')=='owned by fixture'
    result=run('''return {update=function()
    local request,err=sc.save.delete_async('slot')
    assert(request==nil and err:find('disk save directory',1,true))
    sc.debug.watch('memory_fallback',true)
end}''',frames=1,disk=False)
    assert result['watches']['memory_fallback']
print('Async delete: worker commit, exclusion, missing slot, UI failure release and disk preflight passed')
