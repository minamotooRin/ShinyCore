-- Explicit fixed-update timelines. A node has one owner; cancel never snaps values.
local Tween={}
local curves={
    linear=function(t) return t end,
    smooth=function(t) return t*t*(3-2*t) end,
    in_quad=function(t) return t*t end,
    out_quad=function(t) return 1-(1-t)*(1-t) end,
}
local function finite(value)
    return type(value)=="number" and value==value and math.abs(value)<math.huge
end
local function plain(value)
    return type(value)=="table" and getmetatable(value)==nil
end
local function duration(seconds,zero)
    assert(finite(seconds) and (seconds>0 or (zero and seconds==0)),"finite positive duration required")
end
function Tween.new(target,values,seconds,easing)
    duration(seconds)
    assert(plain(target) and plain(values),"plain tween target and values required")
    local goals={}
    for key,value in pairs(values) do
        assert((type(key)=="string" or type(key)=="number") and finite(value) and finite(target[key]),
            "finite numeric tween fields required")
        goals[key]=value
    end
    return {kind="value",target=target,values=goals,duration=seconds,elapsed=0,
        curve=assert(curves[easing or "smooth"],"unknown easing"),done=false,cancelled=false}
end
function Tween.delay(seconds)
    duration(seconds,true)
    return {kind="delay",duration=seconds,elapsed=0,done=seconds==0,cancelled=false}
end
local function group(kind,items)
    assert(plain(items),"dense tween array required")
    local copy,seen={},{}
    for key in pairs(items) do
        assert(type(key)=="number" and key%1==0 and key>=1 and key<=#items,"dense tween array required")
    end
    for i=1,#items do
        local item=items[i]
        assert(plain(item) and (item.kind=="value" or item.kind=="delay" or item.kind=="sequence" or item.kind=="parallel"),
            "tween node required")
        assert(not item.parent and not seen[item],"tween node already owned")
        copy[i]=item;seen[item]=true
    end
    local result={kind=kind,items=copy,index=1,done=#copy==0,cancelled=false}
    for _,item in ipairs(copy) do item.parent=result end
    return result
end
function Tween.sequence(items) return group("sequence",items) end
function Tween.parallel(items) return group("parallel",items) end
local function advance(node,dt)
    if node.done then return true,dt end
    if node.kind=="sequence" then
        while node.index<=#node.items do
            local done,left=advance(node.items[node.index],dt)
            if not done then return false,0 end
            node.index=node.index+1;dt=left
        end
        node.done=true;return true,dt
    elseif node.kind=="parallel" then
        local done,left=true,dt
        for _,item in ipairs(node.items) do
            local finished,remaining=advance(item,dt)
            done=done and finished;left=math.min(left,remaining)
        end
        node.done=done;return done,left
    end
    if node.kind=="value" and not node.from then
        local from={}
        for key in pairs(node.values) do
            assert(finite(node.target[key]),"tween target changed to a nonfinite value")
            from[key]=node.target[key]
        end
        node.from=from -- Capture when this stage starts, after earlier stages finish.
    end
    local used=math.min(dt,node.duration-node.elapsed)
    node.elapsed=node.elapsed+used;node.done=node.elapsed>=node.duration
    if node.kind=="value" then
        local t=node.curve(node.elapsed/node.duration)
        for key,value in pairs(node.values) do
            node.target[key]=node.done and value or (1-t)*node.from[key]+t*value
        end
    end
    return node.done,dt-used
end
-- Returns completion and unused seconds; a sequence passes surplus into its next stage.
function Tween.update(node,dt)
    assert(finite(dt) and dt>=0,"finite nonnegative tween step required")
    assert(not node.parent,"update the owning timeline")
    return advance(node,dt)
end
function Tween.cancel(node)
    if node.done then return end
    if node.items then for _,item in ipairs(node.items) do Tween.cancel(item) end end
    node.done=true;node.cancelled=true
end
return Tween
