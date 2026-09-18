local Tween = {}
local curves = {
    linear=function(t) return t end,
    smooth=function(t) return t*t*(3-2*t) end,
    in_quad=function(t) return t*t end,
    out_quad=function(t) return 1-(1-t)*(1-t) end,
}
function Tween.new(target, values, seconds, easing)
    assert(type(seconds)=="number" and seconds>0, "positive tween duration required")
    local from={}
    for key,value in pairs(values) do
        assert(type(value)=="number" and type(target[key])=="number", "numeric tween fields required")
        from[key]=target[key]
    end
    return {target=target, values=values, from=from, duration=seconds, elapsed=0,
        curve=assert(curves[easing or "smooth"], "unknown easing"), done=false}
end
function Tween.update(tween, dt)
    assert(type(dt)=="number" and dt>=0, "nonnegative tween step required")
    if tween.done then return true end
    tween.elapsed=math.min(tween.duration,tween.elapsed+dt)
    local t=tween.curve(tween.elapsed/tween.duration)
    for key,value in pairs(tween.values) do tween.target[key]=tween.from[key]+(value-tween.from[key])*t end
    tween.done=tween.elapsed>=tween.duration
    return tween.done
end
function Tween.cancel(tween) tween.done=true end
function Tween.parallel(items, dt)
    local done=true
    for _,item in ipairs(items) do if not Tween.update(item,dt) then done=false end end
    return done
end
return Tween
