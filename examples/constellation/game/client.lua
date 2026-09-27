local P=require("game.protocol")
local S=require("shiny.snapshot")
local Client={}
function Client.state(ip,port)
    return {role="client",ip=ip,port=port,token="",id=0,seat=0,room=1,epoch=1,sequence=0,
        phase="connecting",peer=0,heard=sc.net.time(),opened=sc.net.time(),deadline=0,
        notice="Connecting...",objects={},charge=0,complete=false,last_tick=-1}
end
function Client.new(state,handle)
    local timeline=S.new(32,4)
    return {s=state,h=handle,timeline=timeline,shown=S.output(timeline),count=0}
end
local function fail(c,reason)
    if c.h then c.h:close() end
    c.h=nil; c.s.phase,c.s.notice="failed",reason.." / Escape for menu"
end
local function retry(c,now)
    if c.h then c.h:close() end
    c.h=nil; c.s.phase,c.s.peer,c.s.retry_at="retrying",0,now+.5
    if c.s.deadline==0 then c.s.deadline=now+30 end
    c.s.notice="Reconnecting with temporary token..."
end
local function reset_view(c)
    S.reset(c.timeline); c.cursor=nil; c.count=0; c.s.objects={}; c.s.last_tick=-1
end
function Client.update(c,x,y,now,reconnect)
    local s=c.s
    if s.phase=="failed" then return false end
    if s.phase=="retrying" then
        if now>=s.deadline then fail(c,"Reconnect window ended"); return false end
        if now<s.retry_at then return false end
        local h,err=sc.net.join(s.ip,s.port)
        if not h then s.notice=err; s.retry_at=now+1; return false end
        local ok,problem=h:persist("coop")
        if not ok then h:close(); fail(c,problem); return false end
        c.h=h; s.phase,s.opened="connecting",now
    end
    if reconnect and s.phase=="ready" then
        if not c.h:send(s.peer,P.leave()) then retry(c,now); return false end
        s.phase,s.leave_at="leaving",now
    end
    local changed=false
    while c.h do
        local e,err=c.h:poll()
        if err then retry(c,now); return false end
        if not e then break end
        if e.type=="connect" then
            s.peer,s.heard=e.peer,now
            if not c.h:send(e.peer,P.hello(s.token)) then retry(c,now); return false end
            s.phase="handshake"
        elseif e.type=="disconnect" then retry(c,now); return false
        elseif e.type=="receive" then
            local m=P.read(e.data,e.channel)
            if e.peer~=s.peer or not m then fail(c,"Invalid host message"); return false end
            if m.kind==P.WELCOME and s.phase=="handshake" then
                if s.token~="" and (m.token~=s.token or m.id~=s.id or m.seat~=s.seat) then fail(c,"Rejoin identity changed"); return false end
                changed=m.room~=s.room
                s.id,s.seat,s.room,s.epoch,s.token=m.id,m.seat,m.room,m.epoch,m.token
                s.phase,s.notice,s.deadline,s.heard="ready","Connected",0,now
                reset_view(c); sc.log("CONSTELLATION client ready seat "..s.seat)
            elseif m.kind==P.REJECT then
                if m.code==4 and s.token~="" then s.phase,s.leave_at="leaving",now
                else fail(c,m.code==3 and "Server full" or "Join rejected or token expired"); return false end
            elseif m.kind==P.ROOM and (s.phase=="ready" or s.phase=="leaving") then
                if m.epoch>s.epoch then
                    s.room,s.epoch=m.room,m.epoch; reset_view(c); changed=true
                    sc.log("CONSTELLATION client room "..s.room)
                end
                s.heard=now
            elseif m.kind==P.STATE then
                -- Cross-channel snapshots may precede WELCOME or ROOM; ignore those.
                if s.phase=="ready" and m.epoch==s.epoch and m.tick>s.last_tick then
                    local objects={}
                    for i,p in ipairs(m.objects) do objects[i]={id=p.id,x=p.x,y=p.y} end
                    local accepted=S.push(c.timeline,m.tick,m.tick,objects)
                    if not accepted then fail(c,"Invalid snapshot timeline"); return false end
                    s.last_tick,s.objects,s.heard,s.charge=m.tick,m.objects,now,m.charge/1000
                    if m.complete and not s.complete then sc.log("CONSTELLATION complete") end
                    s.complete=m.complete
                    if not c.cursor then c.cursor=math.max(0,m.tick-9) end
                end
            else fail(c,"Unexpected host message"); return false end
        end
    end
    if s.phase=="ready" then
        if now-s.heard>=5 then retry(c,now); return false end
        if sc.tick()%3==0 then
            s.sequence=(s.sequence+1)&0xffffffff
            if not c.h:send(s.peer,P.input(s.sequence,s.epoch,x,y),"state") then retry(c,now); return false end
        end
    elseif s.phase=="leaving" and now-s.leave_at>=2 then retry(c,now); return false
    elseif (s.phase=="handshake" or s.phase=="connecting") and now-s.opened>=5 then retry(c,now); return false end
    if c.cursor then
        local first,last=S.bounds(c.timeline)
        local gap=last-c.cursor
        local speed=gap>12 and 1.1 or (gap<6 and .9 or 1)
        c.cursor=math.max(first,math.min(last,c.cursor+speed))
        c.count,c.status=S.sample(c.timeline,c.cursor,c.shown)
    end
    if c.h then c.h:flush() end
    return changed
end
return Client
