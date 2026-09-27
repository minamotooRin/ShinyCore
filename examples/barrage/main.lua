local Shell=require("shiny.shell")
local UI=require("shiny.ui")
local Challenge=require("challenge")
local View=require("presentation")
local Sound=require("sound")
local sound
local Input=require("shiny.input")
local Controls=require("controls")
local actions
local controller=false
local effects
local player_animation
local shell,run,player,enemies,choice,offers,spark,bullet
local spawn_clock,fire_clock,invulnerable,dash,cooldown,boss_spawned
local aim_x,aim_y=1,0
local kinds={
    scout={health=2,speed=23,size=8,points=10},
    runner={health=2,speed=40,size=7,points=15},
    gunner={health=4,speed=13,size=10,points=25,period=2.7},
    brute={health=12,speed=16,size=15,points=40},
    guardian={health=90,speed=12,size=24,points=300,period=1.3},
}
local function publish()
    local names={}; for _,v in ipairs(offers or {}) do names[#names+1]=v.id end
    sc.debug.watch("game",{wave=run.wave,health=run.health,max_health=run.max_health,score=run.score,
        kills=run.kills,enemies=#enemies,elapsed=run.elapsed,wave_time=run.time,seed=run.seed,rng=run.rng,
        upgrades=run.upgrades,offers=names,mode=choice and "upgrade" or shell.mode,won=run.won,
        shots=run.shots,hits=run.hits,spawned=run.spawned,wounds=run.wounds,dashes=run.dashes})
end
local function burst(x,y,count) sc.particles.burst(spark,x,y,count) end
local function spawn_enemy(name,p)
    local spec=kinds[name]; local edge=Challenge.random(run,1,4)
    local x,y=Challenge.random(run,8,366),Challenge.random(run,36,190)
    if edge==1 then x=4 elseif edge==2 then x=376-spec.size elseif edge==3 then y=30 else y=198-spec.size end
    if (p.x-x)^2+(p.y-y)^2<70^2 then x=374-spec.size-x; y=224-spec.size-y end
    local id=sc.spawn{tag=name,persistent_id="enemy."..(run.spawned+1),x=x,y=y,w=spec.size,h=spec.size,
        sprite=name,frame_w=spec.size,frame_h=spec.size,color="#FFFFFFFF",body=false,solid=true}
    enemies[#enemies+1]={id=id,kind=name,health=spec.health,fire=spec.period or 0,animation=View.enemy_animation(name)}
    run.spawned=run.spawned+1
end
local function projectile(x,y,dx,dy,speed,mask,color)
    return {x=x,y=y,vx=dx*speed,vy=dy*speed,radius=mask==1 and 2 or 3,
        life=3,terrain=false,mask=mask,color=color,sprite=mask==1 and bullet or 0}
end
local function fire_player(p)
    local dx=Input.axis(actions,"aim_left","aim_right")
    local dy=Input.axis(actions,"aim_up","aim_down")
    if dx==0 and dy==0 then
        local nearest=math.huge
        for _,enemy in ipairs(enemies) do
            local e=sc.get(enemy.id); local ex,ey=e.x+e.w*.5-p.x-5,e.y+e.h*.5-p.y-7
            local distance=ex*ex+ey*ey
            if distance<nearest then nearest=distance; dx,dy=ex,ey end
        end
    end
    local length=math.sqrt(dx*dx+dy*dy)
    if length>0 then aim_x,aim_y=dx/length,dy/length end
    local angle=math.atan(aim_y,aim_x); local batch={}
    for i=1,run.spread do
        local a=angle+(i-(run.spread+1)*.5)*.13
        batch[#batch+1]=projectile(p.x+5,p.y+7,math.cos(a),math.sin(a),265,1,0xFFE6ABFF)
    end
    sc.projectiles.spawn(batch); run.shots=run.shots+#batch
    Sound.play(sound,"shot",p.x+5)
end
local function ending()
    local seconds=math.floor(run.elapsed)
    UI.set(shell.ui,"play",{text="NEW CHALLENGE"})
    Shell.finish(shell,run.won and "The last guardian has fallen." or "The lantern fades. Try a different upgrade.",{
        title=run.won and "BEACON DEFENDED" or "LANTERN LOST",
        color=run.won and "#66D9B0FF" or "#FF899AFF",
        details=string.format("SCORE %d / KILLS %d\nTIME %d:%02d / SEED %d",run.score,run.kills,seconds//60,seconds%60,run.seed)})
end
local function finish(won)
    run.won=won; sc.projectiles.clear()
    sc.state.set("result",{seed=run.seed,won=won,score=run.score,kills=run.kills,
        seconds=run.elapsed,upgrades=run.upgrades,health=run.health,rng=run.rng,
        shots=run.shots,hits=run.hits,wounds=run.wounds,dashes=run.dashes,
        wave=run.wave,wave_time=run.time,max_health=run.max_health,spawned=run.spawned})
    sc.state.set("show_result",true)
    local ok,err=sc.save.write("last_result")
    if not ok then shell.notice=err end
    ending()
    Sound.play(sound,won and "win" or "lose")
end
local function hurt(p)
    if invulnerable>0 or dash>0 then return end
    run.health=run.health-1; run.wounds=run.wounds+1; invulnerable=1
    View.hurt(effects,p.x+5,p.y)
    Sound.play(sound,"hurt",p.x+5)
    burst(p.x+5,p.y+7,18)
end
local function choose_upgrade()
    sc.projectiles.clear(); sc.app.pause(true); offers=Challenge.offers(run)
    Sound.play(sound,"wave")
    local children={{id="upgrade_title",kind="label",text="WAVE "..run.wave.." CLEARED",h=24},
        {id="upgrade_hint",kind="label",text=controller and "Choose one upgrade. D-pad / south" or
            "Choose one upgrade. Tab / Enter or mouse",h=20,font_size=11}}
    for _,spec in ipairs(offers) do
        children[#children+1]={id="upgrade_"..spec.id,kind="button",h=32,font_size=11,
            text=spec.name.." / "..spec.description,on_click=function()
                Challenge.upgrade(run,spec.id); choice=nil; offers=nil; spawn_clock=0; fire_clock=0
                invulnerable=1; sc.app.pause(false); Sound.play(sound,"upgrade")
            end}
    end
    choice=UI.new{id="upgrades",kind="overlay",padding=0,children={
        {id="upgrade_panel",kind="modal",x=20,y=20,w=344,h=176,gap=5,children=children}}}
    UI.layout(choice,384,216); choice.focus="upgrade_"..offers[1].id
end
return {
    title="ShinyCore / Barrage",width=384,height=216,gravity=0,ambient=1,
    init=function()
        run=Challenge.new(); enemies={}; spawn_clock=0; fire_clock=0; invulnerable=0; dash=0; cooldown=0; boss_spawned=false
        effects=View.new(); sound=Sound.new(); player_animation=View.player_animation()
        actions=Controls.new()
        sc.projectiles.configure()
        bullet=sc.projectiles.sprite("wisp",0,0,8,10,6,7)
        spark=sc.particles.define{speed_min=20,speed_max=65,life_min=.2,life_max=.55,gravity=0,
            blend="additive",texture={resource="wisp",x=0,y=0,w=8,h=10},
            curve={{time=0,size=5,color=0xFFC98CFF},{time=1,size=0,color=0x70BFFF00}}}
        player=sc.spawn{persistent_id="keeper",tag="player",x=188,y=104,w=10,h=14,sprite="keeper",
            frame_w=12,frame_h=18,body={type="kinematic",sensor=true,category=2,mask=0},layer=2}
        shell=Shell.new("BARRAGE / "..run.seed,"Auto fire / SETTINGS > CONTROLS")
        local previous=sc.state.get("result")
        if previous and not sc.state.get("show_result") then
            shell.notice=string.format("Last challenge: %d points",previous.score)
        end
        UI.set(shell.ui,"checkpoints",{visible=sc.save.read("last_result")~=nil})
        UI.set(shell.ui,"save",{visible=false})
        UI.set(shell.ui,"load",{text="LAST RESULT",on_click=function()
            local ok,err=sc.save.load("last_result"); if not ok then shell.notice=err end
        end})
        UI.set(shell.ui,"play",{on_click=function()
            if shell.mode=="end" then sc.state.set("show_result",false); sc.scene("main.lua")
            else shell.mode="game"; sc.app.pause(false) end
        end})
        sc.audio.music("theme",{loop=true,volume=.07,fade=.4})
        if sc.state.get("show_result") then
            assert(previous and previous.wave and previous.max_health,"invalid saved challenge result")
            for key,value in pairs(previous) do if run[key]~=nil then run[key]=value end end
            run.elapsed=previous.seconds; run.time=previous.wave_time
            ending()
        end
        UI.layout(shell.ui,384,216)
        sc.app.pause(true); publish()
    end,
    update=function(dt)
        controller=sc.input.gamepad_connected()
        Input.update(actions,"ui")
        if choice then
            Input.consume_sources(actions,{all=true})
            sc.app.pause(true); UI.update(choice,dt,384,216,actions)
            Input.update(actions); publish(); return
        end
        local playing=Shell.update(shell,dt,actions)
        Input.update(actions)
        if not playing then publish(); return end
        View.update(effects,dt); Sound.update(sound,dt)
        run.time=run.time+dt; run.elapsed=run.elapsed+dt
        cooldown=math.max(0,cooldown-dt); dash=math.max(0,dash-dt); invulnerable=math.max(0,invulnerable-dt)
        local p=sc.get(player)
        local dx=Input.axis(actions,"left","right")
        local dy=Input.axis(actions,"up","down")
        local length=math.sqrt(dx*dx+dy*dy)
        if length>1 then dx,dy=dx/length,dy/length end
        if length>0 and cooldown==0 and Input.pressed(actions,"dash") then
            dash=.2; cooldown=2; run.dashes=run.dashes+1; Sound.play(sound,"dash",p.x+5)
        end
        local speed=run.speed*(dash>0 and 2.8 or 1)
        p.x=math.max(8,math.min(366,p.x+dx*speed*dt)); p.y=math.max(32,math.min(188,p.y+dy*speed*dt))
        sc.set(player,{x=p.x,y=p.y,flip_x=dx<0,frame=View.animate_player(player_animation,dt,length>0,dash>0),
            color=invulnerable>0 and math.floor(run.elapsed*12)%2==0 and "#FFFFFF70" or "#FFFFFFFF"})
        local damage={}
        for _,hit in ipairs(sc.projectiles.hits()) do
            if hit.target==player then hurt(p)
            elseif hit.target~=0 then damage[hit.target]=(damage[hit.target] or 0)+run.damage; run.hits=run.hits+1 end
        end
        local shots={}
        for i=#enemies,1,-1 do
            local enemy=enemies[i]; local spec=kinds[enemy.kind]; local e=sc.get(enemy.id)
            if damage[enemy.id] then
                View.feedback(effects,e.x,e.y,tostring(damage[enemy.id]),"#FFE6AB")
                Sound.play(sound,"hit",e.x+e.w*.5)
            end
            enemy.health=enemy.health-(damage[enemy.id] or 0)
            if enemy.health<=0 then
                local health=run.health
                Challenge.defeat(run,spec.points); burst(e.x,e.y,enemy.kind=="guardian" and 80 or 10)
                Sound.play(sound,"break",e.x+e.w*.5)
                if run.health>health then
                    View.feedback(effects,p.x,p.y,"+1","#66D9B0"); Sound.play(sound,"heal",p.x+5)
                end
                sc.destroy(enemy.id); table.remove(enemies,i)
            else
                local ex,ey=p.x+5-e.x-e.w*.5,p.y+7-e.y-e.h*.5
                local distance=math.sqrt(ex*ex+ey*ey)
                if distance<7+spec.size*.5 then hurt(p) end
                local speed=spec.speed+run.wave*1.5
                if spec.period and distance<105 then speed=distance<70 and -speed*.5 or 0 end
                if distance>0 then
                    sc.set(enemy.id,{vx=ex/distance*speed,vy=ey/distance*speed,frame=View.animate_enemy(enemy.animation,dt)})
                    enemy.fire=enemy.fire-dt
                    if spec.period and enemy.fire<=0 then
                        enemy.fire=spec.period; local angle=math.atan(ey,ex)
                        local count=enemy.kind=="guardian" and 9 or (run.wave>=4 and 3 or 1)
                        for j=1,count do
                            local a=angle+(j-(count+1)*.5)*.22
                            shots[#shots+1]=projectile(e.x+e.w*.5,e.y+e.h*.5,math.cos(a),math.sin(a),70+run.wave*4,2,0xFF819AFF)
                        end
                    end
                end
            end
        end
        if run.health<=0 then finish(false); publish(); return end
        if #shots>0 then sc.projectiles.spawn(shots) end
        spawn_clock=spawn_clock-dt
        if run.time<Challenge.wave_seconds and spawn_clock<=0 and #enemies<Challenge.max_enemies then
            spawn_clock=math.max(.55,1.5-run.wave*.14)
            local roll=Challenge.random(run,1,10)
            local name=roll<=3 and run.wave>=2 and "gunner" or (roll<=5 and run.wave>=3 and "brute" or (roll<=7 and "runner" or "scout"))
            spawn_enemy(name,p)
        end
        if run.wave==Challenge.waves and run.time>=40 and not boss_spawned then spawn_enemy("guardian",p); boss_spawned=true end
        fire_clock=fire_clock-dt
        if fire_clock<=0 then fire_clock=run.interval; fire_player(p) end
        if run.time>=Challenge.wave_seconds and #enemies==0 then
            if run.wave==Challenge.waves then finish(true) else choose_upgrade() end
        end
        publish()
    end,
    draw=function()
        sc.image("arena",0,0,384,216,{layer=-100})
        local guardian
        local p=sc.get(player)
        for _,enemy in ipairs(enemies) do
            View.enemy(enemy,sc.get(enemy.id),kinds[enemy.kind],p)
            if enemy.kind=="guardian" then guardian=enemy.health/kinds.guardian.health end
        end
        View.draw(effects,run,cooldown,guardian,Controls.hint(actions,"dash",controller))
        Shell.draw(shell)
        if choice then sc.rect(0,0,384,216,"#08101ED8",true); UI.draw(choice) end
    end,
}
