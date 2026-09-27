"""Audio metadata agrees with argument validation, atomic failure and phase rules."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
root=Path(__file__).resolve().parents[1]
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
functions={row['name']:row['contract'] for row in api['functions'] if row['name'].startswith('sc.audio.')}
assert set(functions)=={'sc.audio.play','sc.audio.music','sc.audio.set','sc.audio.stop','sc.audio.bus'}
for name in ['play','music','set','stop']: assert functions['sc.audio.'+name]['phases']==['load','init','update']
assert functions['sc.audio.bus']['mutation']=={'parameter':'options','when':'non_nil','phases':['load','init','update']}
assert 'draw' in functions['sc.audio.bus']['phases'] and 'ui_update' in functions['sc.audio.bus']['phases']
fade=functions['sc.audio.stop']['parameters'][1]
assert (fade['default'],fade['minimum'],fade['maximum'])==(0,0,60)
fields={field['name']:field for field in api['types']['ScAudioOptions']['fields']}
assert set(fields)=={'volume','pitch','pan','fade','loop','paused','persistent','priority','bus'}
music={field['name']:field for field in api['types']['ScMusicOptions']['fields']}
assert set(music)==set(fields)-{'persistent'} and music['bus']['default']=='music'
assert fields['bus']['default_by_resource']=={'music':'music','sound':'sfx'}
assert {name:field['default'] for name,field in fields.items() if 'default' in field}=={
    'volume':1,'pitch':1,'pan':0,'fade':0,'loop':False,'paused':False,'persistent':False,'priority':0}
assert {f['name'] for f in api['types']['ScAudioBusState']['fields']}=={'volume','target','fade','paused'}

checks=[]
for name,field in fields.items():
    if 'minimum' in field:
        for value in [field['minimum'],field['maximum']]:
            checks.append(f"sc.audio.set(id,{{[{json.dumps(name)}]={value}}})")
        for value in [str(field['minimum']-1),str(field['maximum']+1),'false','"1"','0/0','math.huge']:
            checks.append(f"assert(not pcall(sc.audio.set,id,{{[{json.dumps(name)}]={value}}}))")
    elif field['type']=='boolean':
        checks.append(f"assert(not pcall(sc.audio.set,id,{{{name}=1}}))")
    else:
        for bus in ['master','music','sfx','ui']: checks.append(f'sc.audio.set(id,{{bus="{bus}"}})')

with tempfile.TemporaryDirectory(prefix='shiny-audio-contract-') as directory:
    project=Path(directory)
    shutil.copyfile(root/'examples/workshop/assets/chime.wav',project/'chime.wav')
    (project/'project.lua').write_text("return {resources={sound={type='sound',path='chime.wav'}}}",encoding='utf-8')
    (project/'main.lua').write_text('''local id,ui_calls=0,0
sc.audio.bus('ui',{volume=.5}) -- load-phase mutation
local function readonly()
    local state=sc.audio.bus('ui'); assert(state.volume==.5)
    state.volume=0; assert(sc.audio.bus('ui').volume==.5)
    assert(not pcall(sc.audio.bus,'ui',{}))
    assert(not pcall(sc.audio.play,'sound'))
    assert(not pcall(sc.audio.music,'sound'))
    assert(not pcall(sc.audio.set,id,{}))
    assert(not pcall(sc.audio.stop,id))
end
return {init=function()
    id=assert(sc.audio.play('sound',{paused=true}))
'''+ '\n'.join(checks)+'''
    sc.audio.set(id,{volume=.75,fade=0,pitch=1,paused=true})
    assert(not pcall(sc.audio.set,id,{volume=.1,zz_unknown=1}))
    assert(not pcall(sc.audio.set,id,{priority=1.000000001}))
    assert(not pcall(sc.audio.set,id,{priority=-1.000000001}))
    assert(not pcall(sc.audio.set,tostring(id),{}))
    assert(not pcall(sc.audio.play,'sound'..string.char(0)..'extra'))
    assert(not pcall(sc.audio.bus,'ui'..string.char(0)..'extra'))
    assert(not pcall(sc.audio.bus,1))
    assert(not pcall(sc.audio.set,id,setmetatable({},{__index=function() error('must not execute') end})))
    assert(not pcall(sc.audio.bus,'ui',{volume=0,unknown=true}))
    assert(sc.audio.bus('ui').volume==.5)
    for _,options in ipairs({{volume=-.1},{volume=1.1},{fade=-1},{fade=61},{paused=1},{volume=0/0},{fade='1'}}) do
        assert(not pcall(sc.audio.bus,'ui',options))
    end
    for _,fade in ipairs({-1,61,'1',false,math.huge,0/0}) do assert(not pcall(sc.audio.stop,id,fade)) end
    sc.audio.set(id,nil)
end,update=function()
    sc.audio.set(id,{paused=true})
    if sc.tick()==1 then assert(ui_calls>0) end
end,draw=readonly,ui_update=function() ui_calls=ui_calls+1; readonly() end}
''',encoding='utf-8')
    result=subprocess.run([str(binary),str(project),'--headless','--frames','2'],capture_output=True,text=True,encoding='utf-8',timeout=20)
    assert result.returncode==0,result.stderr
    voices=json.loads(result.stdout)['audio']
    assert len(voices)==1 and voices[0]['volume']==.75 and voices[0]['paused'] and voices[0]['position']==0
print('Audio contracts: metadata, limits, atomic errors, full names, strict priority and phase rules passed')
