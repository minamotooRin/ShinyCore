-- Game rules and a reproducible challenge seed, independent of visual randomness.
local C={seed=260926,waves=6,wave_seconds=50,max_enemies=64}
C.upgrades={
    {id="power",name="HOT CORE",description="+1 damage per shot",limit=3},
    {id="rate",name="QUICK SPARK",description="20% shorter fire interval",limit=3},
    {id="spread",name="FAN OF LIGHT",description="Two additional projectiles",limit=3},
    {id="speed",name="TRAIL BOOTS",description="+12 movement speed",limit=3},
    {id="armor",name="HEART LANTERN",description="+2 maximum health; full heal",limit=3},
    {id="repair",name="SECOND WIND",description="Restore all health",limit=99},
}
function C.new()
    return {seed=C.seed,rng=C.seed,wave=1,time=0,elapsed=0,health=6,max_health=6,
        damage=1,interval=.24,spread=1,speed=98,score=0,kills=0,spawned=0,shots=0,hits=0,
        wounds=0,dashes=0,ranks={},upgrades={},won=false}
end
function C.random(run,minimum,maximum)
    run.rng=(run.rng*48271)%2147483647
    return minimum+run.rng%(maximum-minimum+1)
end
function C.offers(run)
    local candidates={}; for _,v in ipairs(C.upgrades) do
        if (run.ranks[v.id] or 0)<v.limit then candidates[#candidates+1]=v end
    end
    local result={}
    for _=1,3 do result[#result+1]=table.remove(candidates,C.random(run,1,#candidates)) end
    return result
end
function C.upgrade(run,id)
    local spec
    for _,v in ipairs(C.upgrades) do if v.id==id then spec=v; break end end
    assert(spec and (run.ranks[id] or 0)<spec.limit,"invalid or exhausted upgrade")
    run.ranks[id]=(run.ranks[id] or 0)+1; run.upgrades[#run.upgrades+1]=id
    if id=="power" then run.damage=run.damage+1
    elseif id=="rate" then run.interval=run.interval*.8
    elseif id=="spread" then run.spread=run.spread+2
    elseif id=="speed" then run.speed=run.speed+12
    elseif id=="armor" then run.max_health=run.max_health+2; run.health=run.max_health
    elseif id=="repair" then run.health=run.max_health end
    run.wave=run.wave+1; run.time=0
end
function C.defeat(run,points)
    run.kills=run.kills+1; run.score=run.score+points
    -- Every twelfth defeat repairs one heart; no random drops or hidden RNG draws.
    if run.kills%12==0 then run.health=math.min(run.max_health,run.health+1) end
end
return C
