"""Opt-in hidden/muted host check: failed room audio stays isolated from the app."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve();root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='shiny-audio-transaction-') as directory:
    project=Path(directory)
    for name in ('chime.wav','theme.ogg'):shutil.copy2(root/'examples/workshop/assets'/name,project/name)
    (project/'project.lua').write_text('return {resources={music={type="music",path="theme.ogg"},sfx={type="sound",path="chime.wav"}}}',encoding='utf-8')
    (project/'main.lua').write_text('''local music,sound
return {width=64,height=64,init=function()
    music=assert(sc.audio.music('music',{loop=true}))
    sound=assert(sc.audio.play('sfx',{loop=true}))
    sc.state.set('music',music);sc.state.set('sound',sound)
end,update=function()
    if sc.tick()==1 then sc.scene('bad.lua') end
    if sc.tick()==3 then
        assert(sc.audio.bus('master').volume==1)
        sc.audio.set(music,{volume=.75});sc.audio.set(sound,{volume=.25})
        sc.state.set('rollback_checked',true);sc.scene('second.lua')
    end
end}''',encoding='utf-8')
    (project/'bad.lua').write_text('''return {width=64,height=64,init=function()
    sc.audio.stop(sc.audio.music('music',{volume=.1}));sc.audio.bus('master',{volume=0})
    error('intentional candidate failure')
end}''',encoding='utf-8')
    (project/'second.lua').write_text('''return {width=64,height=64,init=function()
    assert(sc.state.get('rollback_checked'))
    assert(sc.audio.bus('master').volume==1)
    assert(not pcall(sc.audio.stop,sc.state.get('sound')))
    assert(sc.audio.music('music',{volume=.5})==sc.state.get('music'))
end}''',encoding='utf-8')
    result=subprocess.run([str(binary),str(project),'--frames','8','--capture-hidden','--capture',str(project/'final.png'),
                           '--mute','--save-dir',str(project/'saves')],capture_output=True,text=True,encoding='utf-8',timeout=20)
    assert result.returncode==0,result.stderr
    assert 'intentional candidate failure' in result.stderr,result.stderr
    snapshot=json.loads(result.stdout)
    assert snapshot['scene']=='second.lua' and snapshot['state']['rollback_checked']
    assert len(snapshot['audio'])==1
    voice=snapshot['audio'][0]
    assert voice['id']==snapshot['state']['music'] and voice['volume']==.5
    assert abs(voice['position']-8/60)<.00001,voice
print('Hidden muted host: failed audio draft rollback and successful persistent music commit passed')
