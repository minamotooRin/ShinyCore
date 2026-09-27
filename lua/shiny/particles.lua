-- One Lua update per emitter; particle motion and curves stay native.
local Particles={}
local function number(value,maximum)
    assert(type(value)=="number" and value>=0 and value<=maximum,"particle rate/step outside range")
    return value
end
function Particles.new(spec,rate)
    rate=number(rate or 0,65536)
    return {id=sc.particles.define(spec),rate=rate,remainder=0,enabled=true}
end
function Particles.update(emitter,dt,x,y)
    number(dt,1)
    local rate=number(emitter.rate,65536)
    if not emitter.enabled or sc.app.paused() then return 0 end
    local pending=emitter.remainder+rate*dt
    local count=math.floor(pending)
    if count>0 then sc.particles.burst(emitter.id,x,y,count) end
    -- Keep the fractional emission budget only after successful native commit.
    emitter.remainder=pending-count
    return count
end
function Particles.burst(emitter,x,y,count)
    return sc.particles.burst(emitter.id,x,y,count)
end
return Particles
