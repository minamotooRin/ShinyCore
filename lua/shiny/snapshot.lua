-- Bounded position snapshots. Protocol IDs/ticks belong to the game, not sc entities.
local Snapshot = {}
local function integer(value, low, high)
    return type(value)=="number" and value==math.floor(value) and value>=low and value<=high
end
local function coordinate(value)
    return type(value)=="number" and value>=-1000000000 and value<=1000000000
end
local function frame(buffer, offset)
    return buffer.frames[(buffer.head+offset-2)%buffer.capacity+1]
end

---Allocate all snapshot/object storage up front. Defaults: 32 snapshots, 4 objects.
function Snapshot.new(capacity, max_objects)
    capacity, max_objects = capacity or 32, max_objects or 4
    assert(integer(capacity,2,64), "snapshot capacity requires 2..64")
    assert(integer(max_objects,1,64), "snapshot objects require 1..64")
    local buffer={capacity=capacity,max_objects=max_objects,head=1,count=0,frames={}}
    for i=1,capacity do
        local slot={objects={},count=0}
        for j=1,max_objects do slot.objects[j]={id=0,x=0,y=0} end
        buffer.frames[i]=slot
    end
    return buffer
end

---Allocate reusable sample output once; read only the returned count of entries.
function Snapshot.output(buffer)
    local output={}
    for i=1,buffer.max_objects do output[i]={id=0,x=0,y=0} end
    return output
end

---Clear the timeline after a validated room/session epoch change.
function Snapshot.reset(buffer)
    buffer.head, buffer.count = 1, 0
end

---Copy a whole sorted object snapshot; rejection leaves the previous timeline intact.
---sequence is uint32 (wrap allowed); tick is a strictly increasing server tick.
---Objects are dense {id,x,y} arrays sorted by positive uint32 game ID.
function Snapshot.push(buffer, sequence, tick, objects)
    if not integer(sequence,0,0xffffffff) or not integer(tick,0,2^52-1) then
        return false,"invalid sequence or tick"
    end
    if buffer.count>0 then
        local latest=frame(buffer,buffer.count)
        local distance=(sequence-latest.sequence)&0xffffffff
        if distance==0 or distance>=0x80000000 then return false,"stale" end
        if tick<=latest.tick then return false,"nonincreasing tick" end
    end
    if type(objects)~="table" or getmetatable(objects) then return false,"expected plain objects array" end
    local count=0
    for key in pairs(objects) do
        if not integer(key,1,buffer.max_objects) then return false,"object capacity or array key invalid" end
        count=count+1
    end
    local previous=0
    for i=1,count do
        local object=objects[i]
        if type(object)~="table" or getmetatable(object) or not integer(object.id,1,0xffffffff)
            or object.id<=previous or not coordinate(object.x) or not coordinate(object.y) then
            return false,"objects require sorted unique IDs and finite bounded positions"
        end
        for key in pairs(object) do
            if key~="id" and key~="x" and key~="y" then return false,"unknown object field" end
        end
        previous=object.id
    end
    -- Preflight complete: subsequent writes cannot reject part of the snapshot.
    local slot
    if buffer.count==buffer.capacity then
        slot=frame(buffer,1)
        buffer.head=buffer.head%buffer.capacity+1
    else
        buffer.count=buffer.count+1
        slot=frame(buffer,buffer.count)
    end
    slot.sequence, slot.tick, slot.count = sequence, tick, count
    for i=1,count do
        local source,target=objects[i],slot.objects[i]
        target.id,target.x,target.y=source.id,source.x,source.y
    end
    return true
end

---Sample in server tick units (fractional allowed). Never extrapolates or advances time.
---Writes reusable output; returns count and empty/buffering/interpolated/held.
---Membership changes at the newer snapshot tick; disappearing objects hold their last position.
function Snapshot.sample(buffer, tick, output)
    assert(type(tick)=="number" and tick>=0 and tick<=2^52-1,"invalid sample tick")
    if buffer.count==0 then return 0,"empty" end
    local left,right=frame(buffer,1),nil
    local status="buffering"
    if tick>=left.tick then
        status="held"
        for i=2,buffer.count do
            local next_frame=frame(buffer,i)
            if next_frame.tick>tick then right=next_frame; status="interpolated"; break end
            left=next_frame
        end
    end
    local alpha=right and (tick-left.tick)/(right.tick-left.tick) or 0
    local j=1
    for i=1,left.count do
        local source=left.objects[i]
        local target=assert(output[i],"use Snapshot.output for reusable output")
        local other
        if right then
            while j<=right.count and right.objects[j].id<source.id do j=j+1 end
            if j<=right.count and right.objects[j].id==source.id then other=right.objects[j] end
        end
        target.id=source.id
        target.x=other and source.x+(other.x-source.x)*alpha or source.x
        target.y=other and source.y+(other.y-source.y)*alpha or source.y
    end
    return left.count,status
end

---Read timeline bounds without exposing internal snapshot storage.
function Snapshot.bounds(buffer)
    if buffer.count==0 then return nil,nil end
    return frame(buffer,1).tick,frame(buffer,buffer.count).tick
end
return Snapshot
