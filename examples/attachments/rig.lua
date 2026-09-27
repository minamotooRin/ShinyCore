local Prefab=require('shiny.prefab')
local Rig={}
local definitions={
    ['rig/body']={w=40,h=16,body={type='dynamic',fixed_rotation=false},solid=false,color='#5276B8'},
    ['rig/lamp']={w=12,h=8,body=false,solid=false,color='#FFC98C'},
    ['rig/marker']={w=4,h=4,body=false,solid=false,color='#66D9B0'},
}
function Rig.initial()
    -- Deliberately not parent-first: the batch resolves persistent names before spawning.
    return {format=1,objects={
        {persistent_id='rig/lamp',parent='rig/body',x=48,y=0,angle=.4},
        {persistent_id='rig/marker',parent='rig/lamp',x=18,y=0,angle=0},
        {persistent_id='rig/body',parent=false,x=140,y=80,angle=0,angular_velocity=1.2},
    }}
end
function Rig.spawn(saved)
    assert(type(saved)=='table' and saved.format==1,'unsupported rig format')
    assert(type(saved.objects)=='table' and #saved.objects==3,'rig requires three objects')
    local specs,parents,indices={},{},{}
    for i,row in ipairs(saved.objects) do
        assert(type(row)=='table' and definitions[row.persistent_id] and not indices[row.persistent_id],'unknown or duplicate object persistent ID')
        for key in pairs(row) do
            assert(key=='persistent_id' or key=='parent' or key=='x' or key=='y' or key=='angle' or
                key=='vx' or key=='vy' or key=='angular_velocity','unknown rig record field')
        end
        assert(type(row.x)=='number' and type(row.y)=='number' and type(row.angle)=='number','rig pose required')
        indices[row.persistent_id]=i
        specs[i]=Prefab.merge(definitions[row.persistent_id],{persistent_id=row.persistent_id,
            x=row.x,y=row.y,angle=row.angle,vx=row.vx==nil and 0 or row.vx,vy=row.vy==nil and 0 or row.vy,angular_velocity=row.angular_velocity==nil and 0 or row.angular_velocity})
    end
    for i,row in ipairs(saved.objects) do
        assert(row.parent==false or (type(row.parent)=='string' and indices[row.parent]),'unresolved parent persistent ID')
        parents[i]=row.parent and indices[row.parent] or 0
    end
    return sc.spawn_many(specs,parents)
end
function Rig.capture(ids)
    local objects={}
    for i,id in ipairs(ids) do
        local e=sc.get(id)
        local relation=sc.presentation.attachment(id)
        local pose=relation or e
        objects[i]={persistent_id=e.persistent_id,
            parent=relation and sc.get(relation.parent).persistent_id or false,
            x=pose.x,y=pose.y,angle=pose.angle,vx=e.vx,vy=e.vy,angular_velocity=e.angular_velocity}
    end
    return {format=1,objects=objects}
end
return Rig
