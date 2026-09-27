local R=require("shiny.rejoin")
local P=require("game.protocol")
local C=require("game.config")
local Server={}
local function at(s,peer)
    for _,p in ipairs(s.players) do if p.active and p.peer==peer then return p end end
end
local function detach(s,peer,now)
    R.detach(s.roster,peer,now)
    local p=at(s,peer)
    if p then p.active,p.peer,p.dx,p.dy=false,0,0,0 end
    s.pending[tostring(peer)]=nil
end
local function reject(s,h,peer,code,now)
    s.invalid=s.invalid+1
    h:send(peer,P.reject(code)); h:disconnect(peer)
    detach(s,peer,now)
end
function Server.new()
    local s={role="host",room=1,epoch=1,tick=0,charge=0,complete=false,roster=R.new(3),pending={},players={},rejoins=0,invalid=0}
    for seat=1,4 do
        s.players[seat]={id=seat==1 and 1 or 0,seat=seat,x=100,y=90+seat*50,active=seat==1,
            peer=0,dx=0,dy=0,input_at=0,heard=0,sequence=-1}
    end
    return s
end
function Server.restore(s)
    assert(s.role=="host" and s.room>=1 and s.room<=2)
    s.roster=R.restore(s.roster)
    return s
end
function Server.update(s,h,x,y,now)
    for _,player in ipairs(R.expire(s.roster,now)) do
        for _,p in ipairs(s.players) do if p.id==player+1 then p.id,p.active,p.peer=0,false,0 end end
    end
    while true do
        local e,err=h:poll(); if err then return nil,err end
        if not e then break end
        if e.type=="connect" then s.pending[tostring(e.peer)]={since=now}
        elseif e.type=="disconnect" then detach(s,e.peer,now)
        elseif e.type=="receive" then
            local waiting=s.pending[tostring(e.peer)]
            local player=at(s,e.peer)
            if waiting or player then
                local m=P.read(e.data,e.channel)
                if not m then reject(s,h,e.peer,1,now)
                elseif waiting and m.kind==P.HELLO then
                    if m.token=="" and s.roster.next_player>=0xffffffff then
                        return nil,"player IDs exhausted"
                    end
                    local issued,issue_error
                    if m.token=="" then
                        issued,issue_error=sc.net.token()
                        if not issued then return nil,issue_error end
                    end
                    local id,token=R.join(s.roster,e.peer,m.token,issued,now)
                    if not id then
                        local code=token=="player already connected" and 4 or (token=="player capacity exhausted" and 3 or 2)
                        reject(s,h,e.peer,code,now)
                    else
                        local seat
                        for i,slot in ipairs(s.roster.slots) do if slot.player==id then seat=i+1 end end
                        player=s.players[seat]
                        if m.token=="" then player.x,player.y=C.levels[s.room].start,90+seat*50
                        else s.rejoins=s.rejoins+1; sc.log("CONSTELLATION rejoined player "..(id+1)) end
                        player.id,player.peer,player.active=id+1,e.peer,true
                        player.heard,player.input_at,player.sequence,player.dx,player.dy=now,now,-1,0,0
                        s.pending[tostring(e.peer)]=nil
                        if not h:send(e.peer,P.welcome(id+1,seat,s.room,s.epoch,token)) then detach(s,e.peer,now) end
                        sc.log("CONSTELLATION joined seat "..seat)
                    end
                elseif player and m.kind==P.INPUT then
                    if m.epoch==s.epoch and (player.sequence<0 or P.newer(m.sequence,player.sequence)) then
                        player.sequence,player.dx,player.dy=m.sequence,m.x,m.y
                        player.heard,player.input_at=now,now
                    elseif m.epoch>s.epoch then reject(s,h,e.peer,1,now) end
                elseif player and m.kind==P.LEAVE then
                    detach(s,e.peer,now); h:disconnect(e.peer)
                else reject(s,h,e.peer,1,now) end
            end
        end
    end
    for peer,waiting in pairs(s.pending) do
        if now-waiting.since>=3 then h:disconnect(tonumber(peer)); s.pending[peer]=nil end
    end
    for seat,p in ipairs(s.players) do
        if seat>1 and p.active and now-p.heard>=5 then local peer=p.peer; detach(s,peer,now); h:disconnect(peer) end
        local dx,dy=0,0
        if seat==1 then dx,dy=x,y
        elseif p.active and now-p.input_at<.25 then dx,dy=p.dx,p.dy end
        local scale=dx~=0 and dy~=0 and 1/math.sqrt(2) or 1
        if not s.complete and p.active then
            p.x=math.max(64,math.min(656,p.x+dx*C.speed/60*scale))
            p.y=math.max(110,math.min(310,p.y+dy*C.speed/60*scale))
        end
    end
    local all=true
    for seat,p in ipairs(s.players) do
        if not p.active or math.abs(p.x-C.levels[s.room].goal)>16 or math.abs(p.y-(90+seat*50))>16 then all=false end
    end
    if not s.complete then s.charge=math.max(0,math.min(1,s.charge+(all and 1 or -1)/120)) end
    local changed=false
    if s.charge>=1 and not s.complete then
        if s.room==1 then
            s.room,s.epoch,s.charge=2,2,0; changed=true
            for seat,p in ipairs(s.players) do
                p.x,p.y,p.dx,p.dy=600,90+seat*50,0,0
                if seat>1 and p.active then h:send(p.peer,P.room(s.room,s.epoch)) end
            end
            sc.log("CONSTELLATION host room 2")
        else s.complete=true; sc.log("CONSTELLATION complete"); sc.tone(660,.25,.06) end
    end
    s.tick=s.tick+1
    assert(s.tick<=0xffffffff,"server timeline exhausted")
    if s.tick%3==0 then
        local snapshot=P.state(s)
        for seat,p in ipairs(s.players) do if seat>1 and p.active then h:send(p.peer,snapshot,"state") end end
    end
    return changed
end
return Server
