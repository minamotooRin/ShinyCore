local C=require("game.config")
local Server=require("game.server")
local Client=require("game.client")
local Scene={}
function Scene.new(room)
    local h,s,client,error_text
    local function objects()
        if not s then return {} end
        return s.role=="host" and s.players or s.objects
    end
    return {title="Constellation / Four shared signals",width=720,height=400,gravity=0,ambient=1,
    init=function()
        h=sc.net.available and sc.net.bind("coop") or nil
        s=h and h:state() or nil
        if s then
            if s.role=="host" then s=Server.restore(s) else client=Client.new(s,h) end
        end
    end,
    update=function()
        if sc.key_pressed("escape") then
            if client then h=client.h end
            if h then h:close() end
            sc.scene("main.lua"); return
        end
        if not s or error_text then return end
        local x=(sc.down("right") and 1 or 0)-(sc.down("left") and 1 or 0)
        local y=(sc.down("down") and 1 or 0)-(sc.down("up") and 1 or 0)
        local changed,problem
        if s.role=="host" then
            changed,problem=Server.update(s,h,x,y,sc.net.time())
            if changed==nil then error_text=problem; h:close(); h=nil; return end
            h:flush()
        else
            changed=Client.update(client,x,y,sc.net.time(),sc.key_pressed("r")); h=client.h
        end
        if h then
            local ok,err=h:state(s)
            if not ok then error_text=err; h:close(); h=nil; return end
        end
        local visible={}
        for _,p in ipairs(objects()) do if p.id>0 then visible[#visible+1]={id=p.id,seat=p.seat,x=p.x,y=p.y,active=p.active} end end
        sc.debug.watch("coop",{role=s.role,room=s.room,epoch=s.epoch,complete=s.complete,charge=s.charge,
            players=visible,phase=s.phase or "hosting",rejoins=s.rejoins or 0,invalid=s.invalid or 0})
        if changed then assert(h); sc.scene(C.paths[s.room]) end
    end,
    draw=function()
        sc.rect(0,0,720,400,"#0D192B",true)
        sc.text("C O N S T E L L A T I O N",24,20,24,"#EAF1FF",true)
        sc.text(C.levels[room].name.." / four players, one shared signal",24,56,15,"#9FB6D2",true)
        sc.rect(48,105,624,212,"#16263B",true)
        for seat=1,4 do
            local goal,y=C.levels[room].goal,90+seat*50
            sc.rect(70,y,580,1,"#263A51",true)
            sc.circle(goal,y,18,"#304259",true); sc.circle(goal,y,14,"#142338",true)
            sc.text(tostring(seat),goal-4,y-7,14,C.colors[seat],true)
        end
        local players=objects()
        if client and client.count>0 then
            for i=1,client.count do
                local p=client.shown[i]; local color="#607089"
                for _,raw in ipairs(players) do if raw.id==p.id and raw.active then color=C.colors[raw.seat] end end
                sc.circle(p.x,p.y,8,color,true)
            end
        else
            for _,p in ipairs(players) do if p.id>0 then sc.circle(p.x,p.y,8,p.active and C.colors[p.seat] or "#607089",true) end end
        end
        sc.rect(48,327,624,5,"#263A51",true)
        sc.rect(48,327,624*(s and s.charge or 0),5,"#79DFCA",true)
        local notice=error_text or (not s and "Preview: start a session from the menu" or
            (s.complete and "ALL SIGNALS LINKED / thank you for playing" or
             (s.role=="host" and "Host / hold all four matching rings for two seconds" or s.notice)))
        sc.text(notice,24,344,15,"#EAF1FF",true)
        sc.text("Arrow keys / WASD to move   R: client reconnect   Escape: menu",24,373,13,"#9FB6D2",true)
    end}
end
return Scene
