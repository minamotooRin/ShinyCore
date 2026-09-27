"""Persistent music acquisition uses application voices, not saved runtime handles."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve();root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='shiny-music-') as directory:
    project=Path(directory)
    for name in ('theme','other','third'):
        shutil.copyfile(root/'examples/workshop/assets/theme.ogg',project/(name+'.ogg'))
    shutil.copyfile(root/'examples/workshop/assets/chime.wav',project/'chime.wav')
    (project/'project.lua').write_text('''return {id='music-test',resources={
        theme={type='music',path='theme.ogg'},alias={type='music',path='theme.ogg'},
        other={type='music',path='other.ogg'},third={type='music',path='third.ogg'},
        chime={type='sound',path='chime.wav'}}}''')
    (project/'main.lua').write_text('''local id
return {init=function()
    id=assert(sc.audio.music('theme',{loop=true,volume=.5,priority=127}))
    assert(sc.audio.music('alias')==id)
    for _,options in ipairs({{persistent=true},{persistent=false},{volume=.1,unknown=1},
        {priority=1.5},setmetatable({},{__index=function() error('metamethod') end})}) do
        assert(not pcall(sc.audio.music,'theme',options))
    end
    assert(not pcall(sc.audio.music,'theme',{},'extra'))
    assert(not pcall(sc.audio.music,'chime'))
    assert(not pcall(sc.audio.music,'theme'..string.char(0)))
    local other=assert(sc.audio.music('other',{loop=true,priority=127}))
    local denied,err=sc.audio.music('third')
    assert(denied==nil and err=='audio voice capacity exhausted')
    sc.audio.stop(other)
end,update=function()
    assert(sc.audio.music('alias')==id)
    if sc.tick()==2 then sc.scene('second.lua') end
end,draw=function() assert(not pcall(sc.audio.music,'theme')) end,
ui_update=function() assert(not pcall(sc.audio.music,'theme')) end}''')
    (project/'second.lua').write_text('''local id
return {init=function() id=assert(sc.audio.music('alias',{volume=.25})) end,
update=function()
    assert(sc.audio.music('theme')==id)
    sc.state.set('progress','second room')
    if sc.tick()==0 then assert(sc.save.write('checkpoint')) end
end}''')
    trace=project/'trace.jsonl'
    result=subprocess.run([str(binary),str(project),'--headless','--frames','8','--trace',str(trace),
        '--save-dir',str(project/'saves')],capture_output=True,text=True,encoding='utf-8',timeout=20)
    assert result.returncode==0,result.stderr
    frames=[json.loads(row) for row in trace.read_text(encoding='utf-8').splitlines()]
    voices=[frame['audio'] for frame in frames]
    assert all(len(row)==1 and row[0]['path']=='theme.ogg' for row in voices)
    assert len({row[0]['id'] for row in voices})==1
    for i,row in enumerate(voices):assert abs(row[0]['position']-(i+1)/60)<1e-5,row
    assert voices[0][0]['volume']==.5 and voices[-1][0]['volume']==.25
    assert {frame['scene'] for frame in frames}=={'main.lua','second.lua'}
    saved=json.loads((project/'saves/music-test/checkpoint.json').read_text(encoding='utf-8'))
    assert 'second room' in json.dumps(saved) and json.loads(result.stdout)['state']=={'progress':'second room'}
print('Music: path aliases, reuse/atomic errors, capacity, phases, cross-room ID/position and handle-free state passed')
