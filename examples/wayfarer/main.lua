local Shell=require("shiny.shell")
local UI=require("shiny.ui")
local World=require("shiny.stream_world")
local Quest=require("quest")
local Patrol=require("patrol")
local View=require("presentation")
local Scenery=require("scenery")
local Guide=require("guide")
local traveler_view,courier_view,feedback
local Input=require("shiny.input")
local Controls=require("controls")
local actions
local courier
local theme=require("theme")
local shell,player,bag,inventory,completed,world,focus_x,focus_y,dialogue,healer,stage,equipment,save_error,route_pending
local function clear_route()
    route_pending=false
    sc.state.set("route_cleared",true)
end
local function close_inventory()
    inventory.open=false;inventory.focus=nil;sc.input.focus_text(false)
end
local function update_inventory()
    local items={}
    for i=1,bag do items[i]={id="herb-"..i,label="月光草 #"..i} end
    UI.set(inventory,"items",{items=items})
end
local function publish()
    sc.debug.watch("courier",Patrol.snapshot(courier))
    sc.debug.watch("stream",{chunks=sc.stream.stats().pinned,images=sc.images.stats().pinned,
        route_cleared=sc.state.get("route_cleared")==true})
    local saving=World.status(world)
    sc.debug.watch("save",saving or {status="idle"})
    sc.debug.watch("quest",{collected=bag,remaining=math.max(0,24-bag),complete=completed,stage=stage,equipment=equipment,
        mode=dialogue.open and "dialogue" or (inventory.open and "inventory" or shell.mode)})
end
local function finish()
    UI.set(shell.ui,"play",{text="返回标题",on_click=function() sc.scene("title.lua") end})
    Shell.finish(shell,"药草送达，道路畅通。村庄平安！",{title="任务完成",
        details="月光草 24 / 24   东路畅通\n"..Quest.equipment[equipment].label})
end
local function checkpoint()
    local p=sc.get(player)
    sc.state.set("position",{x=p.x,y=p.y})
    sc.state.set("traveler",inventory.nodes.name.value)
    sc.state.set("courier",Patrol.snapshot(courier))
    local request,err=World.save(world)
    shell.notice=request and "正在保存…" or err
    return request
end
return {
    title="ShinyCore / Wayfarer",width=384,height=216,gravity=0,ambient=1,
    map={tile_size=8,rows={"."},background="#142D27FF"},
    init=function()
        actions=Controls.new()
        route_pending=false
        traveler_view,courier_view,feedback=View.actor(),View.actor(),View.feedback()
        local position=sc.state.get("position") or {x=160,y=100}
        player=sc.spawn({persistent_id="traveler",tag="player",x=position.x,y=position.y,w=8,h=12,layer=2,
            sprite="keeper",frame_w=12,frame_h=18,gravity=0,body={type="dynamic",fixed_rotation=true,friction=0}})
        bag=sc.state.get("herbs") or 0
        stage=sc.state.get("quest_stage") or "meet"; completed=stage=="complete"
        assert(stage=="meet" or stage=="gather" or stage=="complete","unknown saved quest stage")
        equipment=sc.state.get("equipment") or "trail"
        assert(Quest.equipment[equipment],"unknown saved equipment")
        healer=sc.spawn{persistent_id="village:healer",tag="healer",x=208,y=100,w=9,h=13,layer=2,
            sprite="keeper",frame_w=12,frame_h=18,color="#9EC5FFFF",body=false,solid=false}
        courier=Patrol.new(sc.state.get("courier"))
        sc.camera.set{bounds=false,x=position.x-188,y=position.y-102}
        world=World.new{index="maps/world/index.json",name="forest",slot="checkpoint",margin=1,capacity=16,
            residency=true,
            images={["assets/tiles.png"]="forest"},
            prepare=function(object,saved)
                return {tag="herb",x=object.x,y=object.y,w=6,h=6,layer=1,color="#66D9B0FF",
                    sprite="wisp",frame_w=8,frame_h=10,body=false,solid=false},{}
            end,
            export=function(entity,data) return {} end}
        focus_x,focus_y=position.x//256,position.y//256
        World.request(world,{{x=position.x,y=position.y}},0)
        inventory=UI.new({id="inventory",kind="column",x=24,y=26,w=336,h=166,padding=6,gap=3,
            background=theme.panel,children={
            {id="caption",kind="label",text="旅人 / 野外日志",h=22},
            {id="name",kind="input",value=sc.state.get("traveler") or "旅人",h=26,max_bytes=96},
            {id="items",kind="list",h=44,items={}},
            {id="equipment",kind="button",h=22,text=Quest.equipment[equipment].label,on_click=function()
                equipment=equipment=="trail" and "field" or "trail"
                sc.state.set("equipment",equipment)
                UI.set(inventory,"equipment",{text=Quest.equipment[equipment].label})
            end},
            {id="close",kind="button",text="关闭日志",h=24,on_click=close_inventory},
        }},theme)
        dialogue=UI.new({id="conversation",kind="column",x=18,y=30,w=348,h=162,padding=8,gap=6,
            background="#17263EFF",children={
                {id="speaker",kind="label",text="村口药师",h=22,font_size=16},
                {id="words",kind="label",text="",h=78,font_size=16,wrap=332},
                {id="continue",kind="button",text="继续",h=26,on_click=function()
                    dialogue.open=false
                    if completed then finish() end
                end},
            }},theme)
        save_error=UI.new({id="save_error",kind="column",x=36,y=38,w=312,h=140,padding=8,gap=6,
            background=theme.panel,children={
                {id="save_title",kind="label",text="保存未完成",h=24,font_size=18},
                {id="save_message",kind="label",text="当前场景仍保留。请修复问题后重试。",h=34,font_size=14,wrap=296},
                {id="save_retry",kind="button",text="重试保存",h=22,on_click=function() World.retry(world) end},
                {id="save_cancel",kind="button",text="保留当前地图，返回暂停菜单",h=22,on_click=function()
                    World.cancel(world); shell.mode="pause"; focus_x,focus_y=nil,nil
                end},
            }},theme)
        UI.layout(save_error,384,216); save_error.focus="save_retry"
        shell=Shell.new("旅人 · 月光草","探索森林 / 设置中可更改操作")
        shell.ui.theme=theme
        UI.set(shell.ui,"title",{font_size=32,h=40})
        UI.set(shell.ui,"subtitle",{font_size=16})
        UI.set(shell.ui,"play",{text="开始 / 继续"})
        UI.set(shell.ui,"settings",{text="设置"})
        UI.set(shell.ui,"save",{text="保存"})
        UI.set(shell.ui,"load",{text="读取"})
        UI.set(shell.ui,"quit",{text="退出"})
        shell.ui.nodes.save.on_click=checkpoint
        update_inventory()
        if completed then finish() else shell.mode="game"; sc.app.pause(false) end
        assert(sc.audio.music("theme",{loop=true,volume=.07,fade=.4}))
        publish()
    end,
    update=function(dt)
        Input.update(actions)
        local changed,err,event=World.update(world,dt)
        if event=="saved" then shell.notice="已保存" end
        if event=="patched" and route_pending then clear_route() end
        if event=="cancelled" then
            route_pending=false
            shell.notice="已放弃切换；已完成的存档不会撤销"
        end
        if changed then courier.field=nil end
        Patrol.stop(courier)
        if err then shell.notice=err end
        if World.status(world) then sc.set(player,{vx=0,vy=0}); publish(); return end
        local p=sc.get(player)
        sc.camera.set{x=p.x-188,y=p.y-102}
        if dialogue.open then
            Input.consume_sources(actions,{all=true})
            sc.app.pause(true);sc.set(player,{vx=0,vy=0})
            if sc.input.key_pressed("escape") or sc.input.gamepad_pressed("east") then dialogue.nodes.continue.on_click()
            else UI.update(dialogue,dt,384,216,actions) end
            publish();return
        end
        if inventory.open then
            local _,composition=sc.input.text()
            local back=(sc.input.key_pressed("escape") and composition=="") or sc.input.gamepad_pressed("east")
            if inventory.focus=="name" then Input.consume_sources(actions,{keyboard=true}) end
            local toggle=Input.pressed(actions,"journal")
            Input.consume_sources(actions,{all=true})
            sc.app.pause(true);sc.set(player,{vx=0,vy=0})
            if back or toggle then close_inventory() else UI.update(inventory,dt,384,216,actions) end
            publish();return
        end
        if not Shell.update(shell,dt,actions) then sc.set(player,{vx=0,vy=0}); publish(); return end
        if Input.pressed(actions,"journal") then
            inventory.open=true;inventory.focus="name"
            UI.set(inventory,"close",{text="关闭日志 ["..Controls.hint(actions,"journal").."]"})
            UI.layout(inventory,384,216)
            Input.consume_sources(actions,{all=true})
            sc.app.pause(true);sc.set(player,{vx=0,vy=0});publish();return
        end
        sc.app.pause(false)
        View.advance(feedback,dt)
        Patrol.update(courier,world,changed)
        local npc=sc.get(courier.id)
        sc.set(courier.id,View.animate(courier_view,npc.vx,npc.vy,dt))
        local dx=Input.axis(actions,"left","right")
        local dy=Input.axis(actions,"up","down")
        local length=math.max(1,math.sqrt(dx*dx+dy*dy))
        local pose=View.animate(traveler_view,dx,dy,dt)
        pose.vx=dx*Quest.equipment[equipment].speed/length
        pose.vy=dy*Quest.equipment[equipment].speed/length
        sc.set(player,pose)
        local cx,cy=p.x//256,p.y//256
        if not world.region.pending and (cx~=focus_x or cy~=focus_y) then
            sc.state.set("position",{x=p.x,y=p.y})
            World.request(world,{{x=p.x,y=p.y}},sc.tick()+1); focus_x,focus_y=cx,cy
        end
        if Input.pressed(actions,"use") then
            local npc=sc.get(healer)
            if math.abs(p.x-npc.x)<24 and math.abs(p.y-npc.y)<24 then
                local words,finished
                stage,words,finished=Quest.talk(stage,bag,sc.state.get("route_cleared")==true,Controls.hint(actions,"use"))
                completed=stage=="complete"; sc.state.set("quest_stage",stage)
                UI.set(dialogue,"words",{text=words}); UI.layout(dialogue,384,216)
                dialogue.open=true; dialogue.focus="continue"
                sc.set(player,{vx=0,vy=0}); Patrol.stop(courier)
                if finished then checkpoint(); sc.audio.play("chime",{volume=.3}) end
                sc.app.pause(true); publish()
                return
            end
            for _,id in ipairs(sc.find_all("herb")) do
                local h=sc.get(id)
                if Quest.in_reach(p,h,Quest.equipment[equipment].reach) then
                    sc.destroy(id); bag=bag+1; sc.state.set("herbs",bag)
                    sc.emit(h.x,h.y,8,"#66D9B0FF",20,.5); sc.audio.play("chime",{volume=.16,pitch=1.2})
                    View.pickup(feedback,h.x,h.y)
                    update_inventory()
                end
            end
            if not sc.state.get("route_cleared") and math.abs(p.x-328)<24 and math.abs(p.y-128)<24 then
                local count,phase=World.patch(world,{{x=41,y=16,layer=0,gid=4}})
                if count>0 then
                    if phase=="pending" then route_pending=true else clear_route() end
                end
            end
        end
        if Input.pressed(actions,"save") then checkpoint() end
        publish()
    end,
    ui_update=function(dt)
        Input.update(actions,"ui")
        World.ui_update(world)
        local status=World.status(world)
        if status and status.status=="failed" then
            local titles={read="读取未完成",prepare="地图准备未完成",images="图片准备未完成",write="保存未完成",publish="地图切换未完成"}
            UI.set(save_error,"save_title",{text=titles[status.phase]})
            UI.set(save_error,"save_retry",{text=status.phase=="write" and "重试保存" or "重试"})
            Input.consume_sources(actions,{all=true})
            UI.update(save_error,dt,384,216,actions)
        end
    end,
    draw=function()
        World.draw(world)
        Scenery.draw(world)
        if shell.mode=="game" and not inventory.open and not dialogue.open and not World.status(world) then
            local p=sc.get(player)
            local herbs={}
            for _,id in ipairs(sc.find_all("herb")) do
                local herb=sc.get(id);herbs[#herbs+1]=herb
                if Quest.in_reach(p,herb,Quest.equipment[equipment].reach) then View.focus(herb) end
            end
            local target=Guide.target(stage,bag,sc.state.get("route_cleared")==true,p,herbs)
            Guide.draw(p,herbs,target,Quest.equipment[equipment].reach,Controls.hint(actions,"use"))
            View.draw(feedback)
        end
        sc.rect(0,0,384,24,"#101C2EDD",true)
        sc.text("旅人   月光草 "..bag.." / 24    "..Controls.hint(actions,"journal").." 日志",8,3,16,"#E6EDF7FF",true,{font="ui"})
        sc.rect(0,196,384,20,"#101C2EDD",true)
        local objective=Quest.objective(stage,bag,sc.state.get("route_cleared")==true,Controls.hint(actions,"use"))
        if sc.measure(objective,16,"ui")>368 then objective=Quest.objective(stage,bag,sc.state.get("route_cleared")==true,"交互键") end
        sc.text(objective,8,196,16,"#E6EDF7FF",true,{font="ui"})
        if inventory.open then UI.draw(inventory) end
        if dialogue.open then UI.draw(dialogue) end
        Shell.draw(shell)
        local status=World.status(world)
        if status and status.status=="failed" then
            sc.rect(0,0,384,216,"#08101EE8",true); UI.draw(save_error)
        end
    end,
}
