-- Explicit fixed-step animation; events occur when entering a frame.
local Animation={}
function Animation.new(clips)
    for name,clip in pairs(clips) do
        assert(type(name)=="string" and #clip>0, "animation needs named, nonempty clips")
        for _,frame in ipairs(clip) do assert(type(frame.duration)=="number" and frame.duration>0, "frame duration must be positive") end
    end
    return {clips=clips, index=1, elapsed=0, speed=1, done=false}
end
function Animation.play(a,name,restart)
    assert(a.clips[name],"unknown animation: "..tostring(name))
    if a.name==name and not restart then return end
    a.name,a.index,a.elapsed,a.done=name,1,0,false
end
function Animation.update(a,dt)
    assert(dt>=0 and a.speed>=0,"nonnegative animation step and speed required")
    local clip=assert(a.clips[a.name],"select an animation first")
    local events={}
    if not a.done then
        a.elapsed=a.elapsed+dt*a.speed
        local budget=4096
        while a.elapsed>=clip[a.index].duration do
            budget=budget-1; assert(budget>=0,"animation event budget exhausted")
            a.elapsed=a.elapsed-clip[a.index].duration
            if a.index==#clip then
                if not clip.loop then a.done=true; break end
                a.index=1
            else a.index=a.index+1 end
            if clip[a.index].event then events[#events+1]=clip[a.index].event end
        end
    end
    return clip[a.index].frame,a.done,events
end
return Animation
