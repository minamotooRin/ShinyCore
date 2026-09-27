"""Physics metadata, strict arguments, snapshot isolation and phase boundaries."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
functions={row['name'].rsplit('.',1)[1]:row['contract'] for row in api['functions'] if row['name'].startswith('sc.physics.')}
assert set(functions)=={'force','impulse','drop','contacts','ray','query','overlap','sweep','joint','unjoint','joint_control'}
for name,contract in functions.items():
    assert contract['module']=='core'
    assert contract['phases']==(['load','init','update','draw','ui_update'] if name=='contacts' else ['load','init','update'])
assert functions['drop']['parameters'][1]['default']==.2
assert functions['query']['parameters'][4]['default']==0
assert functions['joint']['parameters'][5]['default']==8
assert functions['overlap']['parameters'][1]['maximum']==4096
assert functions['joint_control']['parameters'][1]['type']=='ScJointControlPatch'
assert functions['joint_control']['returns'][0]['type']=='ScJointControl'
for name,fields in {'ScContact':{'a','b','nx','ny','sensor','phase'},
                    'ScRayHit':{'id','x','y','nx','ny','fraction'},
                    'ScJointControl':{'limit','motor','lower','upper','speed','max_effort'}}.items():
    record=api['types'][name]
    assert {f['name'] for f in record['fields']}==fields
    assert all(f['required'] and f['readonly'] for f in record['fields'])
patch=api['types']['ScJointControlPatch']['fields']
assert {f['name'] for f in patch}=={f['name'] for f in api['types']['ScJointControl']['fields']}
assert all(not f['required'] and not f['readonly'] and 'default' not in f for f in patch)

with tempfile.TemporaryDirectory(prefix='shiny-physics-contract-') as folder:
    project=Path(folder)
    (project/'main.lua').write_text(r'''local p=sc.physics
local a,b,j,calls,ui_calls
local function reject(fn,...) assert(not pcall(fn,...)) end
local function equal(a,b)
    for key,value in pairs(a) do assert(b[key]==value,key) end
    for key,value in pairs(b) do assert(a[key]==value,key) end
end
local function readonly()
    local contacts=p.contacts()
    for _,call in ipairs(calls) do reject(call) end
    if contacts[1] then
        local first=p.contacts()[1]; contacts[1].a=-1
        equal(first,p.contacts()[1])
    end
end
assert(#p.contacts()==0)
reject(p.contacts,nil)
return {gravity=0,init=function()
    ui_calls=0
    a=sc.spawn{x=80,y=80,w=8,h=8,body={type='static',sensor=true}}
    b=sc.spawn{x=100,y=80,w=8,h=8,body={type='dynamic'}}
    j=p.joint('distance',a,b,84,84,nil,104,84)
    local defaults=p.joint_control(j)
    assert(not defaults.limit and not defaults.motor and math.abs(defaults.lower-.16)<1e-6 and defaults.upper==4096)
    defaults.motor=true; assert(not p.joint_control(j).motor)
    equal(p.joint_control(j),p.joint_control(j,{}))
    local slider=p.joint('prismatic',a,b,84,84)
    local c=p.joint_control(slider); assert(c.limit and c.lower==0 and c.upper==8)
    p.unjoint(slider)
    local hinge=p.joint('revolute',a,b,84,84)
    c=p.joint_control(hinge); assert(not c.limit and math.abs(c.lower+math.pi)<1e-6 and math.abs(c.upper-math.pi)<1e-6)
    reject(p.joint_control,hinge,{lower=-4})
    p.unjoint(hinge)
    calls={function() p.force(b,1,0) end,function() p.impulse(b,1,0) end,
        function() p.drop(b) end,function() p.ray(64,84,32,0) end,
        function() p.query(80,80,8,8) end,function() p.overlap({84,84},1) end,
        function() p.sweep({64,84},1,32,0) end,function() p.joint('distance',a,b,84,84) end,
        function() p.unjoint(j) end,function() p.joint_control(j) end}
    local valid={force={b,0,0},impulse={b,0,0},drop={b,.2},contacts={},
        ray={64,84,32,0},query={80,80,8,8,0},overlap={{84,84},1},
        sweep={{64,84},1,32,0},joint={'distance',a,b,84,84,8,104,84},
        unjoint={j},joint_control={j,{}}}
    for name,args in pairs(valid) do
        -- Even trailing nil beyond the declared arity is rejected before side effects.
        local count=#args; args[count+1]=false
        reject(p[name],table.unpack(args)); args[count+1]=nil
        reject(p[name],table.unpack(args,1,count+1))
        if name~='contacts' then reject(p[name]) end
    end
    -- Integer-valued numbers are valid; strings, fractions and invalid handles are not.
    assert(p.joint_control(j+0.0).upper==4096)
    for _,id in ipairs({tostring(b),false,0,-1,b+.5,2^52}) do
        reject(p.force,id,0,0); reject(p.impulse,id,0,0); reject(p.drop,id)
        reject(p.joint,'distance',id,b,84,84)
    end
    for _,id in ipairs({tostring(j),false,0,-1,j+.5,2^52}) do
        reject(p.unjoint,id); reject(p.joint_control,id)
    end
    reject(p.joint,'distance'..string.char(0)..'tail',a,b,84,84)
    reject(p.joint,'distance',a,b,84,84,8,104)
    reject(p.joint,'revolute',a,b,84,84,8,104,84)
    reject(p.query,0,0,1,1,math.huge)
    reject(p.query,1e6,0,10,10)
    reject(p.ray,'64',84,32,0)
    reject(p.overlap,{84,84,extra=1},1)
    reject(p.overlap,setmetatable({84,84},{}),1)
    reject(p.overlap,{84,84},0)
    reject(p.overlap,{84,84,84,84},1)
    reject(p.sweep,{64,84},1,0/0,0)
    reject(p.force,b,0,math.huge)
    reject(p.drop,b,-1)
    p.drop(b,0); p.drop(b,nil)
    local before=p.joint_control(j)
    for _,bad in ipairs({{motor=1},{speed='2'},{lower=0},{upper=1e6},
        {speed=1e6+1},{max_effort=-1},{limit=true,lower=20,upper=10},
        {motor=true,unknown=1},setmetatable({motor=true},{})}) do
        reject(p.joint_control,j,bad); equal(before,p.joint_control(j))
    end
    local ids=p.query(80,80,28,8,nil)
    assert(#ids==2 and ids[1]==a and ids[2]==b)
    ids[1]=0; assert(p.query(80,80,28,8)[1]==a)
    assert(#p.query(80,80,0,8)==0)
    local hit=p.ray(64,84,32,0); assert(hit.id==a and hit.fraction>0 and hit.fraction<1)
    hit.id=0; assert(p.ray(64,84,32,0).id==a)
    hit=p.sweep({84,84},1,16,0); assert(hit.id==a and hit.fraction==0 and hit.nx==0 and hit.ny==0)
    sc.spawn{x=80,y=80,w=8,h=8,body={type='dynamic'}} -- Sensor contact for snapshot inspection.
end,update=function()
    if sc.tick()==1 then
        assert(ui_calls>0)
        local contacts=p.contacts(); assert(#contacts>0 and contacts[1].sensor)
        local first=p.contacts()[1]; contacts[1].phase='bad'; equal(first,p.contacts()[1])
    end
end,draw=readonly,ui_update=function() ui_calls=ui_calls+1; readonly() end}
''',encoding='utf-8')
    result=subprocess.run([str(binary),str(project),'--headless','--frames','2'],capture_output=True,text=True,encoding='utf-8',timeout=20)
    assert result.returncode==0,result.stderr
print('Physics contracts: metadata, arguments, handles, shapes, control atomicity and read phases passed')
