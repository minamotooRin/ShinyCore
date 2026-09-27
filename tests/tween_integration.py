"""Timeline time conservation, lazy origins, cancellation and ownership contracts."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='shiny-tween-') as directory:
    project = Path(directory)
    (project / 'lib/shiny').mkdir(parents=True)
    shutil.copyfile(root / 'lua/shiny/tween.lua', project / 'lib/shiny/tween.lua')
    (project / 'main.lua').write_text('''local T=require('shiny.tween')
local function close(a,b) assert(math.abs(a-b)<1e-9,tostring(a)..' ~= '..tostring(b)) end
return {init=function()
    local p={x=0,y=0};local values={x=10}
    local first=T.new(p,values,1,'linear');values.x=100
    local timeline=T.sequence{first,T.new(p,{x=20},1,'linear')}
    assert(not T.update(timeline,1.5));close(p.x,15)
    local done,left=T.update(timeline,.75)
    assert(done);close(left,.25);assert(p.x==20)
    done,left=T.update(timeline,2);assert(done and left==2)
    assert(not pcall(T.update,first,1))

    local function scene()
        local value={x=0,y=0}
        return value,T.sequence{
            T.parallel{T.new(value,{x=12},.5,'linear'),T.new(value,{y=8},1,'linear')},
            T.delay(.25),T.new(value,{x=24},.5,'linear')}
    end
    local a,whole=scene();local b,parts=scene()
    assert(not T.update(whole,1.5))
    for i=1,6 do T.update(parts,.25) end
    close(a.x,18);close(a.x,b.x);close(a.y,b.y)
    done,left=T.update(whole,.5);assert(done);close(left,.25)
    assert(a.x==24 and a.y==8)

    local c={x=1,y=2}
    local unused=T.new(c,{y=10},1)
    local cancelled=T.sequence{T.new(c,{x=9},1,'linear'),unused}
    T.update(cancelled,.25);T.cancel(cancelled);T.update(cancelled,50)
    assert(c.x==3 and c.y==2 and cancelled.cancelled and unused.cancelled)
    local future=T.new(c,{x=17},1,'linear')
    c.x=5;T.update(future,.5);assert(c.x==11)

    local child=T.delay(1)
    assert(not pcall(T.parallel,{child,child}) and child.parent==nil)
    local owner=T.parallel{child}
    assert(not pcall(T.sequence,{T.delay(1),child}) and child.parent==owner)
    assert(not pcall(T.sequence,{[1]=T.delay(1),[3]=T.delay(1)}))
    done,left=T.update(T.sequence{},3);assert(done and left==3)
    done,left=T.update(T.parallel{T.delay(0),T.sequence{}},3);assert(done and left==3)
    for _,bad in ipairs({-1,math.huge,0/0,'1'}) do
        assert(not pcall(T.update,future,bad))
        assert(not pcall(T.new,c,{x=0},bad))
    end
    assert(not pcall(T.new,c,{x=math.huge},1))
    assert(not pcall(T.new,setmetatable({x=1},{}),{x=2},1))
    assert(not pcall(T.delay,-1))
    local large={x=-1e308};local span=T.new(large,{x=1e308},1,'linear')
    T.update(span,.5);assert(large.x==0)
    sc.debug.watch('tween',{passed=true})
end}''', encoding='utf-8')
    result = subprocess.run([str(binary), str(project), '--headless', '--frames', '1'],
                            capture_output=True, text=True, encoding='utf-8', timeout=15)
    assert result.returncode == 0, result.stderr
    assert json.loads(result.stdout)['watches']['tween']['passed']
print('Tween: nested composition, surplus time, cancellation, ownership and finite values passed')
