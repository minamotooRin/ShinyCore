"""Navigation metadata, unreachable results, handle lifetime and atomic steering."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
functions={row['name'].rsplit('.',1)[1]:row['contract'] for row in api['functions'] if row['name'].startswith('sc.navigation.')}
assert set(functions)=={'region','path','mask','flow','direction','refresh','steer'}
for name in ['region','flow','refresh','steer']: assert functions[name]['phases']==['load','init','update']
for name in ['path','mask','direction']: assert 'draw' in functions[name]['phases'] and 'ui_update' in functions[name]['phases']
assert functions['flow']['parameters'][2]['default']==16384
assert functions['flow']['parameters'][3]['default']==1
assert functions['region']['parameters'][3]['default']==8
for name in ['path','flow']:
    radius=functions[name]['parameters'][-1]
    assert radius['name']=='radius' and radius['default']==0 and radius['minimum']==0 and radius['maximum']==4096
assert [r['name'] for r in functions['flow']['returns']]==['handle','status','visited']
assert [r['name'] for r in functions['direction']['returns']]==['dx','dy','status']
assert [r['name'] for r in functions['refresh']['returns']]==['status','visited']
assert {f['name'] for f in api['types']['ScNavigationPath']['fields']}=={'status','visited','points'}
assert {f['name'] for f in api['types']['ScNavigationMask']['fields']}=={'x','y','cell_size','width','height','radius','rows'}

with tempfile.TemporaryDirectory(prefix='shiny-navigation-contract-') as folder:
    project=Path(folder)
    (project/'main.lua').write_text('''local nav=sc.navigation
local field,unit,ui_calls
local function blocked(f)
    local dx,dy,status=nav.direction(f,4,4)
    assert(dx==0 and dy==0 and status=='unreachable')
end
local function readonly()
    assert(nav.path(0,0,3,0).status=='ok')
    local mask=nav.mask(); assert(mask.x==0 and mask.y==0 and mask.rows[1]=='....')
    assert(select(3,nav.direction(field,4,4))=='ok')
    for _,call in ipairs({function() nav.flow(3,0) end,function() nav.region() end,
        function() nav.refresh(field,1) end,function() nav.steer(field,{unit},2) end}) do
        assert(not pcall(call))
    end
end
return {gravity=0,map={tile_size=8,rows={'...#','....','....'}},init=function()
    ui_calls=0
    nav.region(-40,-24,{'....#....','....#....','....#....','.........','....#....','....#....','....#....'},8)
    local mask=nav.mask()
    assert(mask.x==-40 and mask.y==-24 and mask.cell_size==8 and mask.width==9 and mask.height==7
        and mask.radius==0 and #mask.rows==7 and mask.rows[1]:sub(5,5)=='#')
    assert(nav.mask(4.25).rows[1]:sub(1,1)=='#')
    assert(nav.path(2,3,6,3,nil,4).status=='ok')
    assert(nav.path(2,3,6,3,nil,4.25).status=='unreachable')
    local large=nav.flow(6,3,nil,nil,4.25)
    local small=nav.flow(6,3,nil,2,4)
    local dx,dy,status=nav.direction(large,-20,4)
    assert(dx==0 and dy==0 and status=='ok')
    assert(nav.direction(small,-20,4)>0)
    local rejected=nav.path(0,0,6,3,nil,4.25)
    assert(rejected.status=='unreachable' and rejected.visited==0)
    for _,r in ipairs({-1,4097,0/0,math.huge,'4',false}) do
        assert(not pcall(nav.mask,r))
        assert(not pcall(nav.path,2,3,6,3,nil,r))
        assert(not pcall(nav.flow,6,3,nil,2,r))
        assert(nav.direction(small,-20,4)>0)
    end
    nav.region()
    local result=nav.path(0,0,3,0)
    assert(result.status=='unreachable' and result.visited==0 and #result.points==0)
    field=nav.flow(3,0); blocked(field)
    sc.tile(3,0,'.'); assert(select(3,nav.direction(field,4,4))=='stale')
    local status,visited=nav.refresh(field,1); assert(status=='budget_exhausted' and visited==1)
    local dx,dy=nav.direction(field,4,4); assert(dx==0 and dy==0)
    repeat local before=visited; status,visited=nav.refresh(field,2); assert(visited>=before and visited<=before+2) until status~='budget_exhausted'
    assert(status=='ok')
    result=nav.path(0,0,3,0); assert(result.status=='ok' and #result.points==4 and result.points[4].x==3)
    assert(nav.path(0,0,3,0,1).status=='budget_exhausted')
    local old=field; field=nav.flow(3,0); assert(field~=old and not pcall(nav.direction,old,4,4))
    assert(not pcall(nav.flow,99,0)); assert(select(3,nav.direction(field,4,4))=='ok')
    local other=nav.flow(0,0,16384,2)
    assert(select(3,nav.direction(field,4,4))=='ok')
    assert(not pcall(nav.region,0,0,{'..','.'})); assert(select(3,nav.direction(field,4,4))=='ok')
    nav.region(-16,-16,{'....','....'})
    old=field; field=nav.flow(3,0)
    assert(not pcall(nav.direction,old,4,4) and not pcall(nav.direction,other,4,4))
    dx,dy,status=nav.direction(field,-12,-12); assert(dx>0 and math.abs(dy)<.001 and status=='ok')
    nav.region(); old=field; field=nav.flow(3,0)
    assert(not pcall(nav.refresh,old,1))
    unit=sc.spawn{x=0,y=0,w=8,h=8,vx=7,vy=9,body=false}
    local stale=sc.spawn{}; sc.destroy(stale)
    for _,batch in ipairs({{unit,stale},{unit,unit},{[2]=unit},{unit,extra=true},{tostring(unit)},
        setmetatable({unit},{__len=function() error('must not execute') end})}) do
        assert(not pcall(nav.steer,field,batch,20))
        local u=sc.get(unit); assert(u.vx==7 and u.vy==9)
    end
    assert(nav.steer(field,{},20)==0 and nav.steer(field,{unit},20)==1)
    assert(sc.get(unit).vx>0 and math.abs(sc.get(unit).vy)<.001)
    for _,call in ipairs({function() nav.path('0',0,3,0) end,function() nav.path(0,0,3,0,0) end,
        function() nav.path(0,0,3,0,1048577) end,function() nav.path(0,0,3.5,0) end,
        function() nav.path(0,0,3,0,1,2,3) end,function() nav.flow(3,0,1,17) end,
        function() nav.flow(3,0,1,0) end,function() nav.flow(3,0,'1') end,
        function() nav.direction(tostring(field),4,4) end,function() nav.direction(field,0/0,0) end,
        function() nav.region(0,0,{'.'},0) end,function() nav.region(0,0,{'.'},257) end,
        function() nav.region(1000000,0,{'.'}) end,function() nav.region(nil) end,
        function() nav.steer(field,{unit},-1) end,function() nav.steer(field,{unit},1000001) end}) do
        assert(not pcall(call))
    end
    sc.set(unit,{vx=0,vy=0})
end,update=function()
    if sc.tick()==1 then
        assert(ui_calls>0)
        -- Deliberately carry a runtime handle only to test rejection in the next room.
        sc.state.set('stale_flow',field); sc.scene('next.lua')
    end
end,draw=readonly,ui_update=function() ui_calls=ui_calls+1; readonly() end}
''',encoding='utf-8')
    (project/'next.lua').write_text('''return {map={tile_size=8,rows={'....'}},init=function()
local current=sc.navigation.flow(3,0)
assert(not pcall(sc.navigation.direction,sc.state.get('stale_flow'),4,4))
assert(select(3,sc.navigation.direction(current,4,4))=='ok')
end}''',encoding='utf-8')
    result=subprocess.run([str(binary),str(project),'--headless','--frames','3'],capture_output=True,text=True,encoding='utf-8',timeout=20)
    assert result.returncode==0,result.stderr
    assert json.loads(result.stdout)['scene']=='next.lua'
print('Navigation contracts: circular clearance, budgets, blocked goals, stale handles, region/room changes and atomic steering passed')
