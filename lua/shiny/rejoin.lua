-- Host-owned reservations. Keep them in session:state(), never in save slots.
local Rejoin = {}
local function integer(value,low,high)
    return type(value)=="number" and value==math.floor(value) and value>=low and value<=high
end
local function token(value)
    return type(value)=="string" and #value==32 and not value:find("[^0-9a-f]")
end
local function clock(roster,now)
    assert(type(now)=="number" and now>=roster.time and now<=2^40,"nondecreasing application time required")
    roster.time=now
end
local function clear(slot)
    slot.player,slot.peer,slot.token,slot.expires=0,0,"",0
end

---Reserve at most capacity remote players (default 3, plus the local host).
function Rejoin.new(capacity)
    capacity=capacity or 3
    assert(integer(capacity,1,32),"rejoin capacity requires 1..32")
    local roster={version=1,capacity=capacity,next_player=1,time=0,slots={}}
    for i=1,capacity do roster.slots[i]={player=0,peer=0,token="",expires=0} end
    return roster
end

---Fresh join: presented="", issued=host-generated unique 32 lowercase hex bytes.
---Resume: presented=previous token, issued=nil. Returns player,token or nil,error.
function Rejoin.join(roster,peer,presented,issued,now)
    assert(integer(peer,1,0xffffffff),"invalid peer")
    clock(roster,now)
    if presented~="" and not token(presented) then return nil,"invalid token" end
    if presented=="" and not token(issued) then return nil,"invalid issued token" end
    for _,slot in ipairs(roster.slots) do
        if slot.peer==peer then return nil,"peer already joined" end
        if presented=="" and slot.token==issued then return nil,"issued token already in use" end
    end
    if presented~="" then
        for _,slot in ipairs(roster.slots) do
            if slot.token==presented then
                if slot.peer~=0 then return nil,"player already connected" end
                if now>=slot.expires then return nil,"token expired" end
                slot.peer,slot.expires=peer,0
                return slot.player,slot.token
            end
        end
        return nil,"unknown token"
    end
    if roster.next_player>0xffffffff then return nil,"player IDs exhausted" end
    for _,slot in ipairs(roster.slots) do
        if slot.player==0 then
            slot.player,slot.peer,slot.token=roster.next_player,peer,issued
            roster.next_player=roster.next_player+1
            return slot.player,slot.token
        end
    end
    return nil,"player capacity exhausted"
end

---Start the 30-second window once. Late duplicate disconnects cannot extend it.
function Rejoin.detach(roster,peer,now)
    assert(integer(peer,1,0xffffffff),"invalid peer")
    clock(roster,now)
    for _,slot in ipairs(roster.slots) do
        if slot.peer==peer then
            slot.peer,slot.expires=0,now+30
            return slot.player
        end
    end
    return nil
end

---Call before processing joins. Returns expired game player IDs for explicit cleanup.
function Rejoin.expire(roster,now)
    clock(roster,now)
    local expired={}
    for _,slot in ipairs(roster.slots) do
        if slot.player~=0 and slot.peer==0 and now>=slot.expires then
            expired[#expired+1]=slot.player
            clear(slot)
        end
    end
    return expired
end

function Rejoin.player(roster,peer)
    if not integer(peer,1,0xffffffff) then return nil end
    for _,slot in ipairs(roster.slots) do if slot.peer==peer then return slot.player end end
    return nil
end

---Validate and copy same-application session:state() data after a room change.
---No sockets or credentials are restored across application restarts.
function Rejoin.restore(data)
    local function plain(value,fields)
        assert(type(value)=="table" and not getmetatable(value),"plain rejoin data required")
        for key in pairs(value) do assert(fields[key],"unknown rejoin field") end
    end
    plain(data,{version=true,capacity=true,next_player=true,time=true,slots=true})
    assert(data.version==1,"unsupported rejoin version")
    assert(integer(data.capacity,1,32),"invalid rejoin capacity")
    local roster=Rejoin.new(data.capacity)
    clock(roster,data.time)
    assert(integer(data.next_player,1,0x100000000),"invalid next player ID")
    roster.next_player=data.next_player
    assert(type(data.slots)=="table" and not getmetatable(data.slots),"plain slots required")
    local count=0
    for key in pairs(data.slots) do
        assert(integer(key,1,roster.capacity),"invalid slot key"); count=count+1
    end
    assert(count==roster.capacity,"invalid slot count")
    local players,peers,tokens={},{},{}
    for i=1,roster.capacity do
        local source,target=data.slots[i],roster.slots[i]
        plain(source,{player=true,peer=true,token=true,expires=true})
        assert(integer(source.player,0,roster.next_player-1) and integer(source.peer,0,0xffffffff),"invalid player or peer")
        if source.player==0 then
            assert(source.peer==0 and source.token=="" and source.expires==0,"invalid empty slot")
        else
            assert(token(source.token) and not players[source.player] and not tokens[source.token],"duplicate player or token")
            players[source.player],tokens[source.token]=true,true
            if source.peer==0 then
                assert(type(source.expires)=="number" and source.expires>0 and source.expires<=roster.time+30,"invalid expiry")
            else
                assert(source.expires==0 and not peers[source.peer],"duplicate peer or invalid connected expiry")
                peers[source.peer]=true
            end
            target.player,target.peer,target.token,target.expires=source.player,source.peer,source.token,source.expires
        end
    end
    return roster
end
return Rejoin
