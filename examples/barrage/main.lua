local Shell=require("shiny.shell")
local shell,player,enemies,clock,wave,health,score
local function enemy(x,y)
    return sc.spawn({tag="enemy",x=x,y=y,w=7,h=7,color="#FF727CFF",body=false})
end
return {
    title="ShinyCore / Barrage",width=384,height=216,gravity=0,ambient=1,
    init=function()
        sc.projectiles.configure(32768)
        player=sc.spawn({tag="player",x=188,y=104,w=8,h=8,color="#66D9B0FF",solid=false})
        enemies={}; clock=0; wave=0; health=5; score=sc.state.get("score") or 0
        shell=Shell.new("BARRAGE","WASD move / arrows fire / survive 5 waves")
    end,
    update=function(dt)
        if not Shell.update(shell,dt) then return end
        clock=clock+dt
        local desired=math.min(5,math.floor(clock/45)+1)
        if desired>wave then
            wave=desired
            for i=1,20+wave*10 do enemies[#enemies+1]=enemy(sc.random(10,360),sc.random(10,190)) end
        end
        local p=sc.get(player)
        local dx=(sc.input.key_down("d") and 1 or 0)-(sc.input.key_down("a") and 1 or 0)
        local dy=(sc.input.key_down("s") and 1 or 0)-(sc.input.key_down("w") and 1 or 0)
        sc.set(player,{x=math.max(2,math.min(374,p.x+dx*95*dt)),y=math.max(2,math.min(206,p.y+dy*95*dt))})
        if sc.tick()%6==0 then
            local aimx=(sc.input.key_down("right") and 1 or 0)-(sc.input.key_down("left") and 1 or 0)
            local aimy=(sc.input.key_down("down") and 1 or 0)-(sc.input.key_down("up") and 1 or 0)
            if aimx==0 and aimy==0 then aimx=1 end
            local length=math.sqrt(aimx*aimx+aimy*aimy)
            sc.projectiles.spawn({{x=p.x+4,y=p.y+4,vx=aimx/length*260,vy=aimy/length*260,life=2,terrain=false,color=0xFFCB77FF}})
        end
        local killed={}
        for _,hit in ipairs(sc.projectiles.hits()) do if hit.target~=0 then killed[hit.target]=true end end
        for i=#enemies,1,-1 do
            local id=enemies[i]
            if killed[id] then sc.destroy(id); table.remove(enemies,i); score=score+1
            else
                local e=sc.get(id); local ex,ey=p.x-e.x,p.y-e.y; local distance=math.sqrt(ex*ex+ey*ey)
                if distance>8 then sc.set(id,{vx=ex/distance*(12+wave*3),vy=ey/distance*(12+wave*3)})
                else
                    health=health-1; sc.destroy(id); table.remove(enemies,i)
                    sc.emit(p.x,p.y,12,"#FF727CFF",30,.5)
                end
            end
        end
        sc.state.set("score",score)
        if health<=0 then Shell.finish(shell,"Try again: "..score.." enemies defeated")
        elseif clock>=225 and #enemies==0 then Shell.finish(shell,"All waves cleared. Score "..score) end
        sc.debug.watch("game",{wave=wave,health=health,score=score,enemies=#enemies})
    end,
    draw=function()
        sc.text("BARRAGE   wave "..wave.." / 5   health "..health.."   score "..score,8,8,10,"#E6EDF7FF",true)
        Shell.draw(shell)
    end,
}
