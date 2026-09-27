-- One shared native field per patrol target, rebuilt only after topology changes.
local World=require("shiny.stream_world")
local Patrol={}
local stops={{x=324,y=204},{x=452,y=252}}
function Patrol.new(saved)
    saved=saved or {x=324,y=204,target=2}
    assert(saved.target==1 or saved.target==2,"invalid patrol target")
    return {id=sc.spawn{persistent_id="village:courier",tag="courier",x=saved.x,y=saved.y,
        w=8,h=12,layer=2,color="#D7A8FFFF",sprite="keeper",frame_w=12,frame_h=18,
        gravity=0,body={type="dynamic",friction=0,fixed_rotation=true}},target=saved.target}
end
function Patrol.snapshot(patrol)
    local entity=sc.get(patrol.id)
    return {x=entity.x,y=entity.y,target=patrol.target}
end
function Patrol.stop(patrol) sc.set(patrol.id,{vx=0,vy=0}) end
function Patrol.update(patrol,world,changed)
    if changed then patrol.field=nil end
    local entity=sc.get(patrol.id)
    local target=stops[patrol.target]
    if math.abs(entity.x-target.x)<5 and math.abs(entity.y-target.y)<5 then
        patrol.target=patrol.target%#stops+1; target=stops[patrol.target]; patrol.field=nil
    end
    -- The village courier waits when either endpoint is outside active chunks.
    if not World.contains(world,{x=entity.x,y=entity.y,w=8,h=12})
        or not World.contains(world,{x=target.x,y=target.y,w=8,h=12}) then
        Patrol.stop(patrol); return
    end
    if not patrol.field then
        patrol.field=World.flow(world,target.x+4,target.y+6,256,2)
        if not patrol.field then Patrol.stop(patrol); return end
    end
    local status=sc.navigation.refresh(patrol.field,256)
    if status=="ok" then sc.navigation.steer(patrol.field,{patrol.id},38) else Patrol.stop(patrol) end
end
return Patrol
