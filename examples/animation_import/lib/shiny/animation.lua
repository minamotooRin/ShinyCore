-- Fixed-step sprite timelines. Play queues entry; update returns ordered events.
local Animation={}
local function finite(value) return type(value)=="number" and value==value and math.abs(value)<math.huge end
local function plain(value,label)
    assert(type(value)=="table" and getmetatable(value)==nil,label.." must be a plain table")
end
function Animation.new(clips)
    plain(clips,"clips")
    local owned={}
    for name,clip in pairs(clips) do
        assert(type(name)=="string" and name~="","animation needs named clips")
        plain(clip,"clip "..name)
        local n=#clip
        assert(n>0,"clip "..name.." must contain frames")
        for key in pairs(clip) do
            assert(key=="loop" or (type(key)=="number" and key%1==0 and key>=1 and key<=n),"invalid clip field")
        end
        assert(clip.loop==nil or type(clip.loop)=="boolean","clip.loop must be boolean")
        local frames={loop=clip.loop or false}
        for i=1,n do
            local f=clip[i];plain(f,"clip "..name.." frame "..i)
            for key in pairs(f) do assert(key=="frame" or key=="duration" or key=="event","invalid frame field") end
            assert(finite(f.frame) and f.frame%1==0 and f.frame>=0 and f.frame<=65535,"frame must be an integer in 0..65535")
            assert(finite(f.duration) and f.duration>0,"frame duration must be finite and positive")
            assert(f.event==nil or (type(f.event)=="string" and #f.event>0 and #f.event<=128),"event must be a nonempty string up to 128 bytes")
            frames[i]={frame=f.frame,duration=f.duration,event=f.event}
        end
        owned[name]=frames
    end
    assert(next(owned),"animation needs at least one clip")
    return {clips=owned,index=1,elapsed=0,speed=1,done=false,pending=false}
end
function Animation.play(a,name,restart)
    assert(type(name)=="string" and a.clips[name],"unknown animation: "..tostring(name))
    assert(restart==nil or type(restart)=="boolean","restart must be boolean")
    if a.name==name and not restart then return false end
    a.name,a.index,a.elapsed,a.done,a.pending=name,1,0,false,true
    return true
end
function Animation.frame(a)
    local clip=assert(a.clips[a.name],"select an animation first")
    return clip[a.index].frame,a.done
end
function Animation.update(a,dt)
    assert(finite(dt) and dt>=0 and finite(a.speed) and a.speed>=0,"finite nonnegative step and speed required")
    local clip=assert(a.clips[a.name],"select an animation first")
    local elapsed=a.elapsed+(a.done and 0 or dt*a.speed)
    assert(finite(elapsed),"animation time overflow")
    local index,done=a.index,a.done
    local events={}
    if a.pending and clip[index].event then events[1]=clip[index].event end
    local steps=0
    while not done and elapsed>=clip[index].duration do
        steps=steps+1;assert(steps<=4096,"animation transition budget exhausted")
        elapsed=elapsed-clip[index].duration
        if index==#clip and not clip.loop then
            done=true;elapsed=clip[index].duration
        else
            index=index==#clip and 1 or index+1
            if clip[index].event then events[#events+1]=clip[index].event end
        end
    end
    -- All validation/budget checks precede publication, including queued entry.
    a.index,a.elapsed,a.done,a.pending=index,elapsed,done,false
    return clip[index].frame,done,events
end
return Animation
