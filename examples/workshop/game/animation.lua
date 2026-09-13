-- Ordinary Lua: per-frame seconds, optional marker, no state changes in draw.
local Animation = {}
function Animation.new(clips)
    return {clips=clips, name=nil, index=1, elapsed=0, done=false}
end
function Animation.play(a, name)
    assert(a.clips[name], "unknown animation: "..tostring(name))
    if a.name == name then return end
    a.name, a.index, a.elapsed, a.done = name, 1, 0, false
end
function Animation.update(a, dt)
    assert(type(dt)=="number" and dt>=0 and dt<=1, "invalid animation step")
    local clip=assert(a.clips[a.name], "select an animation first")
    local markers={}
    if not a.done then
        a.elapsed=a.elapsed+dt
        local budget=256
        while a.elapsed >= clip[a.index].duration do
            budget=budget-1; assert(budget>0, "animation exceeds step budget")
            assert(clip[a.index].duration>0, "frame duration must be positive")
            a.elapsed=a.elapsed-clip[a.index].duration
            a.index=a.index+1
            if a.index>#clip then
                if clip.loop then a.index=1 else a.index=#clip; a.done=true; break end
            end
            if clip[a.index].marker then markers[#markers+1]=clip[a.index].marker end
        end
    end
    return clip[a.index].frame, a.done, markers
end
return Animation
