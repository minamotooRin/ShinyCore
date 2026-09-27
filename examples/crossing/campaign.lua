-- Persist authored progress only; native handles and solver state stay in the room.
local Campaign={paths={"main.lua","rooms/mill.lua","rooms/beacon.lua"},signal_order={1,3,2}}
local function number(n,low,high) return type(n)=="number" and n>=low and n<=high end
function Campaign.new(saved)
    local c={version=1,room=1,started=false,complete=false,stages={}}
    for i=1,3 do c.stages[i]={lights={},open=false,sequence=0,time=0,deaths=0,
        position={x=24,y=174},crate={x=300,y=176}} end
    if not saved then return c end
    assert(type(saved)=="table" and saved.version==1,"unsupported Crossing campaign version")
    assert(number(saved.room,1,3) and saved.room%1==0 and type(saved.started)=="boolean" and type(saved.complete)=="boolean","invalid campaign header")
    assert(type(saved.stages)=="table" and #saved.stages==3,"invalid campaign stages")
    c.room,c.started,c.complete=saved.room,saved.started,saved.complete
    for i,stage in ipairs(saved.stages) do
        local out=c.stages[i]
        assert(type(stage)=="table" and type(stage.lights)=="table" and type(stage.open)=="boolean","invalid stage progress")
        for id,value in pairs(stage.lights) do
            assert(type(id)=="string" and id:match("^light%.[1-5]$") and value==true,"invalid collected light ID")
            out.lights[id]=true
        end
        assert(number(stage.sequence,0,3) and stage.sequence%1==0 and number(stage.time,0,1e9) and
            number(stage.deaths,0,1e9) and stage.deaths%1==0,"invalid stage counters")
        for _,key in ipairs({"position","crate"}) do
            local p=stage[key]
            assert(type(p)=="table" and number(p.x,8,1264) and number(p.y,-64,260),"invalid saved "..key)
            out[key]={x=p.x,y=p.y}
        end
        out.open,out.sequence,out.time,out.deaths=stage.open,stage.sequence,stage.time,stage.deaths
    end
    if c.complete then
        assert(c.room==3,"completed campaign must be in the beacon room")
        for _,stage in ipairs(c.stages) do assert(Campaign.ready(stage),"completed campaign has unfinished crossings") end
        assert(c.stages[3].sequence==3,"completed campaign has an unfinished signal")
    end
    return c
end
function Campaign.count(stage)
    local n=0; for _ in pairs(stage.lights) do n=n+1 end; return n
end
function Campaign.collect(stage,id)
    if stage.lights[id] then return false end
    stage.lights[id]=true; return true
end
function Campaign.interact(c,room,index)
    local stage=c.stages[room]
    if stage.open then return "The crossing is powered." end
    if room==1 then
        if Campaign.count(stage)<2 then return "The lever needs two lights. Explore the west bank." end
        stage.open=true; return "Bridge power restored. Ride the ferry across the gap."
    elseif room==3 then
        stage.sequence=index==Campaign.signal_order[stage.sequence+1] and stage.sequence+1 or (index==1 and 1 or 0)
        stage.open=stage.sequence==3
        return stage.open and "The beacon is aligned. Bring all five lights to the exit." or
            "Signal "..stage.sequence.." / 3. The inscription says WEST, EAST, CENTER."
    end
    return "Push the crate onto the amber plate."
end
function Campaign.plate(stage,x,bottom)
    if x>=460 and x<=500 and bottom>=188 and bottom<=196 then stage.open=true end
end
function Campaign.ready(stage) return stage.open and Campaign.count(stage)==5 end
return Campaign
