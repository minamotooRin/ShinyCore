-- Versioned game messages; independent of native handles and platform byte order.
local P={HELLO=1,WELCOME=2,INPUT=3,ROOM=4,STATE=5,LEAVE=6,REJECT=7}
local prefix="CS"..string.char(1)
local function packet(kind,format,...) return prefix..string.char(kind)..string.pack(">"..format,...) end
function P.newer(a,b) local d=(a-b)&0xffffffff; return d>0 and d<0x80000000 end
function P.hello(token) return packet(P.HELLO,"")..token end
function P.welcome(id,seat,room,epoch,token) return packet(P.WELCOME,"I4BBI4c32",id,seat,room,epoch,token) end
function P.input(sequence,epoch,x,y) return packet(P.INPUT,"I4I4bb",sequence,epoch,x,y) end
function P.room(room,epoch) return packet(P.ROOM,"BI4",room,epoch) end
function P.leave() return packet(P.LEAVE,"") end
function P.reject(code) return packet(P.REJECT,"B",code) end
function P.state(s)
    local objects={}
    for _,p in ipairs(s.players) do if p.id>0 then objects[#objects+1]=p end end
    table.sort(objects,function(a,b) return a.id<b.id end)
    local chunks={packet(P.STATE,"I4I4BI2BB",s.tick,s.epoch,s.room,math.floor(s.charge*1000+.5),s.complete and 1 or 0,#objects)}
    for _,p in ipairs(objects) do
        chunks[#chunks+1]=string.pack(">I4BI2I2B",p.id,p.seat,math.floor(p.x*8+.5),math.floor(p.y*8+.5),p.active and 1 or 0)
    end
    return table.concat(chunks)
end
function P.read(data,channel)
    if type(data)~="string" or #data<4 or data:sub(1,3)~=prefix then return nil,"header" end
    local kind=data:byte(4)
    local expected=(kind==P.INPUT or kind==P.STATE) and "state" or "reliable"
    if channel~=expected then return nil,"channel" end
    local m={kind=kind}
    if kind==P.HELLO then
        m.token=data:sub(5)
        if #m.token~=0 and (#m.token~=32 or m.token:find("[^0-9a-f]")) then return nil,"token" end
    elseif kind==P.WELCOME then
        if #data~=46 then return nil,"welcome length" end
        m.id,m.seat,m.room,m.epoch,m.token=string.unpack(">I4BBI4c32",data,5)
        if m.id<2 or m.seat<2 or m.seat>4 or m.room<1 or m.room>2 or m.epoch~=m.room or m.token:find("[^0-9a-f]") then return nil,"welcome fields" end
    elseif kind==P.INPUT then
        if #data~=14 then return nil,"input length" end
        m.sequence,m.epoch,m.x,m.y=string.unpack(">I4I4bb",data,5)
        if math.abs(m.x)>1 or math.abs(m.y)>1 then return nil,"direction" end
    elseif kind==P.ROOM then
        if #data~=9 then return nil,"room length" end
        m.room,m.epoch=string.unpack(">BI4",data,5)
        if m.room<1 or m.room>2 or m.epoch~=m.room then return nil,"room fields" end
    elseif kind==P.STATE then
        if #data<17 then return nil,"snapshot length" end
        local complete,count
        m.tick,m.epoch,m.room,m.charge,complete,count=string.unpack(">I4I4BI2BB",data,5)
        if count<1 or count>4 or #data~=17+count*10 or m.room<1 or m.room>2 or m.epoch~=m.room or m.charge>1000 or complete>1 then return nil,"snapshot fields" end
        m.complete,m.objects=complete==1,{}
        local previous,seats=0,{}
        for i=1,count do
            local id,seat,x,y,active=string.unpack(">I4BI2I2B",data,18+(i-1)*10)
            if id<=previous or seat<1 or seat>4 or seats[seat] or x<64*8 or x>656*8 or y<110*8 or y>310*8 or active>1 then return nil,"snapshot object" end
            previous,seats[seat]=id,true
            m.objects[i]={id=id,seat=seat,x=x/8,y=y/8,active=active==1}
        end
    elseif kind==P.LEAVE then
        if #data~=4 then return nil,"leave length" end
    elseif kind==P.REJECT then
        if #data~=5 or data:byte(5)<1 or data:byte(5)>4 then return nil,"reject code" end
        m.code=data:byte(5)
    else return nil,"message kind" end
    return m
end
return P
