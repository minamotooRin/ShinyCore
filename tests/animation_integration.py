"""Native Lua animation contracts: immutable data, entry events, bounds and atomic failure."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[1];binary=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='shiny-animation-') as directory:
    project=Path(directory);(project/'lib/shiny').mkdir(parents=True)
    shutil.copyfile(root/'lua/shiny/animation.lua',project/'lib/shiny/animation.lua')
    (project/'project.lua').write_text('return {limits={entities=1,particles=0,draws=1}}',encoding='utf-8')
    (project/'main.lua').write_text(r'''local A=require('shiny.animation')
local function reject(fn,...) assert(not pcall(fn,...)) end
return {init=function()
 local source={walk={loop=true,{frame=0,duration=.25,event='start'},{frame=1,duration=.25,event='step'}},
   once={{frame=7,duration=.25,event='end'}},still={{frame=3,duration=1}}}
 local a=A.new(source);source.walk[1].duration=100;source.walk[2].event='mutated'
 reject(A.frame,a);reject(A.update,a,0);reject(A.play,a,'missing');reject(A.play,a,'walk','yes')
 assert(A.play(a,'walk'));assert(A.frame(a)==0)
 local f,done,events=A.update(a,0);assert(f==0 and not done and table.concat(events)== 'start')
 assert(#select(3,A.update(a,0))==0 and not A.play(a,'walk'))
 f,done,events=A.update(a,.75);assert(f==1 and not done and table.concat(events,',')=='step,start,step')
 a.speed=.5;f,done,events=A.update(a,.5);assert(f==0 and events[1]=='start')
 a.speed=0;A.play(a,'once');f,done,events=A.update(a,10)
 assert(f==7 and not done and #events==1 and events[1]=='end' and a.elapsed==0)
 a.speed=1;f,done,events=A.update(a,.5);assert(f==7 and done and #events==0)
 assert(not A.play(a,'once') and select(2,A.frame(a)))
 assert(#select(3,A.update(a,100))==0)
 assert(A.play(a,'once',true));assert(select(3,A.update(a,0))[1]=='end')
 A.play(a,'walk');A.play(a,'still');assert(#select(3,A.update(a,0))==0)
 for _,dt in ipairs({-1,math.huge,0/0}) do reject(A.update,a,dt) end
 for _,speed in ipairs({-1,math.huge,0/0}) do a.speed=speed;reject(A.update,a,0) end
 a.speed=1
 local fast=A.new{run={loop=true,{frame=0,duration=1,event='pulse'}}};A.play(fast,'run')
 reject(A.update,fast,4097)
 assert(fast.index==1 and fast.elapsed==0 and not fast.done and fast.pending)
 assert(select(3,A.update(fast,0))[1]=='pulse')
 A.play(fast,'run',true);f,done,events=A.update(fast,4096);assert(#events==4097 and not done)
 fast.speed=1e308;reject(A.update,fast,1e308)
 assert(fast.elapsed==0 and fast.index==1 and not fast.done)
 local touched=false
 reject(A.new,setmetatable({},{__pairs=function() touched=true;return next,{},nil end}));assert(not touched)
 reject(A.new,{})
 reject(A.new,{x={loop='yes',{frame=0,duration=1}}})
 reject(A.new,{x={{frame=0,duration=1},[3]={frame=1,duration=1}}})
 reject(A.new,{x={{frame=0,duration=1},extra=true}})
 reject(A.new,{x={setmetatable({frame=0,duration=1},{})}})
 for _,value in ipairs({0,-1,math.huge,0/0}) do reject(A.new,{x={{frame=0,duration=value}}}) end
 for _,value in ipairs({-.5,.5,65536,math.huge,0/0}) do reject(A.new,{x={{frame=value,duration=1}}}) end
 reject(A.new,{x={{frame=0,duration=1,event=false}}})
 reject(A.new,{x={{frame=0,duration=1,event=string.rep('x',129)}}})
 reject(A.new,{x={{frame=0,duration=1,unknown=1}}})
end}''',encoding='utf-8')
    result=subprocess.run([str(binary),str(project),'--headless','--frames','1'],capture_output=True,timeout=20)
    assert result.returncode==0,result.stderr.decode('utf-8',errors='replace')
print('animation: validation, copied clips, entry/loop events, speed/completion and atomic budget failure passed')
