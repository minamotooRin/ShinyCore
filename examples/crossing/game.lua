local Shell=require("shiny.shell")
local UI=require("shiny.ui")
local Prefab=require("shiny.prefab")
local Campaign=require("campaign")
local levels=require("levels")
local Guide=require("guide")
local Input=require("shiny.input")
local Controls=require("controls")
local View=require("presentation")
local Game={}
local cues={
    jump={volume=.13,priority=1,bus="sfx"},land={volume=.11,priority=1,bus="sfx"},
    switch={volume=.19,priority=4,bus="sfx"},error={volume=.16,priority=3,bus="sfx"},
    rescue={volume=.22,priority=6,bus="sfx"},
}
local function sound(name) sc.audio.play(name,cues[name]) end
local light={entity={tag="light",w=7,h=7,color="#70BFFFFF",body=false,solid=false},components={kind="power"}}
local function terrain(level)
    local rows={}
    for y=0,26 do
        local row={}
        for x=0,159 do
            local gap=level.gap and x*8>=level.gap[1] and x*8<level.gap[2]
            row[#row+1]=(x==0 or x==159 or (y>=24 and not gap)) and "#" or "."
        end
        rows[#rows+1]=table.concat(row)
    end
    return {tile_size=8,rows=rows,color=level.color,accent=level.accent,background="#111C2EFF"}
end
function Game.room(index)
    local level=levels[index]
    local c,stage,shell,player,gate,platform,crate,lights,controls,notice,respawn
    local notice_time=0
    local actions,animation,plate,airborne,ground_seen,dust,ending_save_failed,finish_ready,load_ready
    local request,request_kind,pending_error,pending_error_kind
    local function notify(text)
        if #text>120 then
            local last=0
            for _,boundary in ipairs(sc.input.boundaries(text)) do if boundary>121 then break end; last=boundary-1 end
            text=text:sub(1,last).."..."
        end
        notice=text; notice_time=4
    end
    local function objective()
        return Guide.current(index,stage,sc.get(player).x,crate and sc.get(crate).x,platform and sc.get(platform).x,
            Controls.hint(actions,"use",sc.input.gamepad_connected()))
    end
    local function finish()
        local seconds,deaths=0,0
        for _,part in ipairs(c.stages) do seconds=seconds+part.time; deaths=deaths+part.deaths end
        seconds=math.floor(seconds)
        UI.set(shell.ui,"play",{text="NEW JOURNEY"})
        Shell.finish(shell,"All three crossings shine again.",{title="BEACON RESTORED",
            details=string.format("15 LIGHTS / 3 CROSSINGS\nTIME %d:%02d / RESCUES %d",seconds//60,seconds%60,deaths)})
    end
    local function publish()
        sc.debug.watch("crossing",{room=index,name=level.name,collected=Campaign.count(stage),open=stage.open,
            sequence=stage.sequence,deaths=stage.deaths,complete=c.complete,
            save_pending=request~=nil,save_error=ending_save_failed,
            mode=shell.mode,player=sc.get(player).x,
            objective=objective().text})
    end
    local function snapshot()
        local p=sc.get(player)
        stage.position={x=p.x,y=p.y}
        if crate then local b=sc.get(crate); stage.crate={x=b.x,y=b.y} end
        sc.state.set("campaign",c)
    end
    local function finish_saved()
        finish_ready=false; ending_save_failed=false; c.complete=true
        sc.state.set("campaign",c); finish()
    end
    local function busy(value)
        UI.set(shell.ui,"save",{disabled=value})
        UI.set(shell.ui,"load",{disabled=value})
    end
    local function save_failure(ending,err)
        shell.notice=err
        if not ending then notify(err); return end
        c.complete=false; snapshot(); ending_save_failed=true
        if shell.mode=="game" then sc.app.pause(false) end
        local hint=Controls.hint(actions,"save",sc.input.gamepad_connected())
        if hint=="-" then hint=Controls.hint(actions,"save",false) end
        notify("Ending not saved. Press "..hint.." or use SAVE in the menu to retry.")
    end
    local function checkpoint(ending)
        if ending then c.complete=true end
        snapshot()
        local id,err=sc.save.write_async("checkpoint")
        if not id and err and err:find("disk save directory",1,true) then
            local ok,reason=sc.save.write("checkpoint")
            if not ok then save_failure(ending,reason); return end
            shell.notice="Crossing saved"; notify(shell.notice)
            if ending then finish_ready=true end
            return
        end
        if not id then save_failure(ending,err); return end
        -- The worker owns the completed snapshot; the active room is unfinished until it commits.
        if ending then c.complete=false; sc.state.set("campaign",c); sc.app.pause(true) end
        request,request_kind=id,ending and "ending" or "save"
        busy(true); shell.notice="Saving checkpoint..."; notify(shell.notice)
    end
    local function load_checkpoint()
        local id,err=sc.save.read_chunks_async("checkpoint",{})
        if not id and err and err:find("disk save directory",1,true) then
            local ok,reason=sc.save.load("checkpoint")
            if not ok then shell.notice=reason; notify(reason) end
            return ok
        end
        if not id then shell.notice=err; notify(err); return false end
        request,request_kind=id,"load"
        busy(true); shell.notice="Reading checkpoint..."; notify(shell.notice)
        return true
    end
    local function reset_player()
        stage.deaths=stage.deaths+1
        sound("rescue");airborne=false;ground_seen=false
        sc.set(player,{x=24,y=174,vx=0,vy=0,frame=0,flip_x=false,sprite="keeper"})
        animation=View.player()
        if crate then sc.set(crate,{x=300,y=176,vx=0,vy=0}) end
        notify("Back at camp. Collected lights and powered gates are kept.")
        respawn=.25
    end
    return {
        title="ShinyCore / Crossing / "..level.name,width=384,height=216,gravity=550,ambient=1,map=terrain(level),
        init=function()
            actions=Controls.new(); animation=View.player(); airborne=false; ground_seen=false
            ending_save_failed=false; finish_ready=false; load_ready=false
            dust=sc.particles.define{speed_min=12,speed_max=34,life_min=.24,life_max=.42,
                angle_min=-math.pi,angle_max=0,gravity=.6,
                curve={{time=0,size=3,color=0xE5DFC6D8},{time=1,size=0,color=0xE5DFC600}}}
            c=Campaign.new(sc.state.get("campaign")); c.room=index; stage=c.stages[index]
            player=sc.spawn{persistent_id="traveler",tag="player",x=stage.position.x,y=stage.position.y,w=10,h=16,
                sprite="keeper",frame_w=12,frame_h=18,layer=2,body={type="dynamic",shape="capsule",friction=0,fixed_rotation=true}}
            lights={}; controls={}; respawn=0
            for i,p in ipairs(level.lights) do
                local id="light."..i
                if not stage.lights[id] then lights[id]=Prefab.spawn(light,{entity={persistent_id=id,x=p[1],y=p[2]}})
                else sc.identity.declare(id); sc.identity.remove(id) end
            end
            for i,p in ipairs(level.ledges) do
                sc.spawn{persistent_id="ledge."..i,x=p[1],y=p[2],w=p[3],h=8,color=level.accent,body={type="static",one_way=true}}
            end
            if level.platform then
                local p=level.platform; local t=(1-math.cos(stage.time*2*math.pi/p.period))*.5
                platform=sc.spawn{persistent_id="ferry",tag="platform",x=p.x+p.dx*t,y=p.y+p.dy*t,w=p.w,h=8,
                    color="#66D9B0FF",body={type="kinematic",one_way=true}}
            end
            if level.crate then
                crate=sc.spawn{persistent_id="mill.crate",tag="crate",x=stage.crate.x,y=stage.crate.y,w=16,h=16,
                    color="#DCA95EFF",body={type="dynamic",density=.4,friction=.3,fixed_rotation=true}}
                plate=sc.spawn{persistent_id="mill.plate",x=460,y=189,w=40,h=3,color=stage.open and "#66D9B0FF" or "#FFCB77FF",body=false,solid=false}
            end
            if level.slope then
                sc.spawn{persistent_id="mill.ramp",x=760,y=160,w=96,h=32,color=level.accent,
                    body={type="static",shape="polygon",vertices={0,32,96,0,96,32}}}
                sc.spawn{persistent_id="mill.deck",x=856,y=160,w=160,h=32,color=level.color,body={type="static"}}
            end
            for i,p in ipairs(level.controls) do
                controls[i]=sc.spawn{persistent_id="switch."..i,x=p[1],y=p[2],w=10,h=18,color=View.control(index,stage,i),body=false,solid=false}
            end
            if not stage.open then gate=sc.spawn{persistent_id="exit.gate",x=1040,y=128,w=12,h=64,color="#C37589FF",body={type="static"}} end
            sc.spawn{persistent_id="exit.marker",x=1216,y=148,w=18,h=44,color="#66D9B077",body=false,solid=false}
            sc.camera.follow(player)
            shell=Shell.new("CROSSING / "..index,level.name,function() c.started=true end)
            if c.started then shell.mode="game" end
            shell.ui.nodes.save.on_click=function()
                checkpoint(ending_save_failed)
            end
            shell.ui.nodes.load.on_click=load_checkpoint
            local play=shell.ui.nodes.play.on_click
            shell.ui.nodes.play.on_click=function(...)
                if c.complete then sc.state.set("campaign",Campaign.new()); sc.scene("main.lua")
                else play(...) end
            end
            if c.complete then finish() end
            sc.app.pause(shell.mode~="game")
            assert(sc.audio.music("theme",{loop=true,volume=.08,fade=.4}))
            publish()
        end,
        update=function(dt)
            Input.update(actions,"ui")
            if pending_error then
                save_failure(pending_error_kind=="ending",pending_error)
                pending_error,pending_error_kind=nil,nil
            end
            local playing=Shell.update(shell,dt,actions)
            if finish_ready then finish_saved(); publish(); return end
            if load_ready then
                load_ready=false
                local ok,err=sc.save.load("checkpoint")
                if not ok then shell.notice=err; notify(err) end
                publish(); return
            end
            Input.update(actions)
            if not playing then publish(); return end
            if c.complete then finish(); publish(); return end
            notice_time=math.max(0,notice_time-dt); if notice_time==0 then notice=nil end
            stage.time=stage.time+dt; respawn=math.max(0,respawn-dt)
            local p=sc.get(player)
            local in_water=level.gap and p.x+p.w>level.gap[1] and p.x<level.gap[2] and p.y>192
            if in_water or p.y>244 or Input.pressed(actions,"rescue") then reset_player(); publish(); return end
            if p.grounded then
                if airborne then
                    sound("land")
                    sc.particles.burst(dust,p.x+p.w*.5,p.y+p.h-2,10)
                end
                airborne=false;ground_seen=true
            elseif ground_seen and math.abs(p.vy)>15 then airborne=true end
            local direction=Input.axis(actions,"left","right")
            local patch={vx=respawn>0 and 0 or direction*90}
            if p.grounded and Input.pressed(actions,"jump") then
                patch.vy=-240; sound("jump")
                sc.particles.burst(dust,p.x+p.w*.5,p.y+p.h-2,7)
            end
            patch.frame,patch.flip_x,patch.sprite=View.animate(animation,p,patch.vx/90,dt,patch.vy~=nil)
            sc.set(player,patch)
            if platform then
                local target=level.platform; local t=(1-math.cos(stage.time*2*math.pi/target.period))*.5
                local b=sc.get(platform)
                sc.set(platform,{vx=(target.x+target.dx*t-b.x)/dt,vy=(target.y+target.dy*t-b.y)/dt})
            end
            for i=1,5 do
                local id="light."..i; local instance=lights[id]
                if instance then
                    local l=sc.get(instance.id)
                    if math.abs(l.x-p.x)<12 and math.abs(l.y-p.y)<20 then
                        Campaign.collect(stage,id); Prefab.destroy(instance); lights[id]=nil
                        sc.emit(l.x,l.y,12,"#70BFFFFF",24,.7); sc.audio.play("chime",{volume=.25,pitch=1+i*.05})
                    end
                end
            end
            if crate then local b=sc.get(crate); Campaign.plate(stage,b.x+b.w*.5,b.y+b.h) end
            if Input.pressed(actions,"use") then
                for i,id in ipairs(controls) do
                    local b=sc.get(id)
                    if math.abs(b.x-p.x)<28 and math.abs(b.y-p.y)<32 then
                        local sequence=stage.sequence
                        notify(Campaign.interact(c,index,i))
                        sound((stage.open or stage.sequence>sequence) and "switch" or "error")
                        break
                    end
                end
            end
            for i,id in ipairs(controls) do
                local color=View.control(index,stage,i)
                if sc.get(id).color~=color then sc.set(id,{color=color}) end
            end
            if plate then sc.set(plate,{color=stage.open and "#66D9B0FF" or "#FFCB77FF"}) end
            if stage.open and gate then sc.destroy(gate); gate=nil; sc.audio.play("chime",{volume=.4,pitch=.7}) end
            if Input.pressed(actions,"save") then
                checkpoint(ending_save_failed)
                if request or finish_ready then
                    if finish_ready then finish_saved() end
                    publish(); return
                end
            end
            if Input.pressed(actions,"load") then
                if load_checkpoint() then publish(); return end
            end
            if p.x>1200 then
                if Campaign.ready(stage) then
                    if index==3 then
                        if not ending_save_failed then
                            checkpoint(true)
                            if finish_ready then finish_saved() end
                        end
                    else snapshot(); sc.scene(Campaign.paths[index+1]) end
                else notify("The exit needs five lights and a powered gate.") end
            end
            publish()
        end,
        ui_update=function(dt)
            if not request then return end
            local status=sc.save.status(request)
            if status.status=="pending" then
                Input.update(actions,"ui"); Shell.update(shell,dt,actions)
                return
            end
            local kind=request_kind
            local record=kind=="load" and status.status=="complete" and sc.save.result(request).record
            sc.save.release(request)
            request,request_kind=nil,nil; busy(false)
            if status.status=="failed" then
                pending_error,pending_error_kind=status.error,kind
            elseif kind=="load" then
                if record then load_ready=true
                else pending_error,pending_error_kind="Save slot does not exist.","load" end
            elseif kind=="ending" then finish_ready=true
            else shell.notice="Crossing saved"; notify(shell.notice) end
        end,
        draw=function()
            local pad=sc.input.gamepad_connected()
            local use=Controls.hint(actions,"use",pad)
            -- Three panorama rows; stop at the ground and leave side walls visible.
            sc.image("scenery",8,-24,1264,216,{source_y=(index-1)*314,source_w=1672,source_h=313,
                layer=-100,color="#A6ADC4FF"})
            local near
            local p=sc.get(player)
            for i,id in ipairs(controls) do
                local b=sc.get(id)
                if math.abs(b.x-p.x)<28 and math.abs(b.y-p.y)<32 then
                    near={x=b.x,y=b.y,label=level.controls[i][3],index=i}; break
                end
            end
            if level.gap then sc.rect(level.gap[1],190,level.gap[2]-level.gap[1],4,"#70BFFFFF") end
            for i,p in ipairs(level.controls) do
                local _,state=View.control(index,stage,i)
                View.marker(p[1]-7,p[2]+7,state)
                if not near or near.index~=i then sc.text(p[3].." ["..use.."]",p[1]-10,p[2]-16,10,"#FFCB77FF",false) end
            end
            View.world(index,level,stage,platform,crate,gate,controls)
            if level.crate then
                sc.text(stage.open and "PLATE POWERED" or "PRESSURE PLATE",440,164,10,"#FFCB77FF",false)
                View.marker(480,182,stage.open and "done" or "ready")
            end
            if index<3 then sc.text("NEXT CROSSING",1180,128,10,"#66D9B0FF",false) end
            Guide.draw(index,stage,objective(),notice,near,Controls.legend(actions,pad),use)
            Shell.draw(shell)
        end,
    }
end
return Game
