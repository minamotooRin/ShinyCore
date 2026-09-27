-- Presentation reads campaign state; it never changes puzzle or physics rules.
local Animation=require("shiny.animation")
local Campaign=require("campaign")
local View={}
local colors={locked="#B59159FF",ready="#FFCB77FF",done="#66D9B0FF"}
function View.player()
    local clock=Animation.new{
        idle={{frame=0,duration=.5},loop=true},
        walk={{frame=1,duration=.125},{frame=2,duration=.125},{frame=3,duration=.125},loop=true},
        rise={{frame=2,duration=.2}},fall={{frame=3,duration=.2}},
    }
    Animation.play(clock,"idle")
    return {clock=clock,left=false}
end
function View.animate(player,pose,direction,dt,jumping)
    if direction~=0 then player.left=direction<0 end
    local name=jumping and "rise" or not pose.grounded and (pose.vy<0 and "rise" or "fall") or
        direction~=0 and "walk" or "idle"
    Animation.play(player.clock,name)
    player.clock.speed=name=="walk" and math.max(.35,math.abs(direction)) or 1
    local frame=Animation.update(player.clock,dt)
    return frame,player.left
end
function View.control(room,stage,index)
    if stage.open then return colors.done,"done" end
    if room==1 then
        local state=Campaign.count(stage)>=2 and "ready" or "locked"
        return colors[state],state
    end
    for step,control in ipairs(Campaign.signal_order) do
        if control==index then
            local state=step<=stage.sequence and "done" or step==stage.sequence+1 and "ready" or "locked"
            return colors[state],state
        end
    end
end
function View.marker(x,y,state)
    -- Distinct geometry supplements color: hollow/filled diamond, then check mark.
    if state=="done" then
        sc.rect(x-3,y,2,2,colors.done);sc.rect(x-1,y+2,2,2,colors.done)
        sc.rect(x+1,y,2,2,colors.done);sc.rect(x+3,y-2,2,2,colors.done)
    else
        local color=colors[state]
        sc.rect(x-1,y-3,2,2,color);sc.rect(x-3,y-1,2,2,color)
        sc.rect(x+1,y-1,2,2,color);sc.rect(x-1,y+1,2,2,color)
        if state=="ready" then sc.rect(x-1,y-1,2,2,color) end
    end
end
local trim={
    {edge="#8EC4D0B0",sprig="#648AACCC",light="#D5F0E4CC"},
    {edge="#A5BE8DB0",sprig="#73946BCC",light="#E3DBA9CC"},
    {edge="#BDA3CBB0",sprig="#8C76AACC",light="#EDD9F4CC"},
}
function View.world(index,level,stage,platform,crate,gate,controls)
    local colors=trim[index]
    for x=48,1200,72 do
        if not level.gap or x+8<level.gap[1] or x>level.gap[2] then
            sc.rect(x,191,13,1,colors.edge)
            sc.rect(x+4,188,1,3,colors.sprig)
            sc.rect(x+7,189,1,2,colors.sprig)
        end
    end
    if level.gap then
        local phase=math.floor(stage.time*8)
        for i=0,3 do
            local x=level.gap[1]+8+(i*23+phase*3)%(level.gap[2]-level.gap[1]-24)
            sc.rect(x,183+i%2*4,12,1,"#92CDE2AA")
        end
    end
    for _,ledge in ipairs(level.ledges) do
        local x,y,w=ledge[1],ledge[2],ledge[3]
        sc.rect(x+2,y+1,w-4,1,colors.light)
        for at=x+8,x+w-8,16 do sc.rect(at,y+4,3,2,"#223447BB") end
    end
    if platform then
        local p=sc.get(platform)
        sc.rect(p.x+2,p.y+1,p.w-4,2,colors.light)
        sc.rect(p.x+2,p.y+6,p.w-4,1,"#1C3445DD")
        for x=p.x+8,p.x+p.w-8,12 do sc.rect(x,p.y+3,2,2,"#263C58DD") end
    end
    if crate then
        local p=sc.get(crate)
        sc.rect(p.x+2,p.y+2,p.w-4,2,"#F0D392DD")
        sc.rect(p.x+3,p.y+5,2,p.h-8,"#795640CC")
        sc.rect(p.x+p.w-5,p.y+5,2,p.h-8,"#795640CC")
        sc.rect(p.x+4,p.y+p.h-5,p.w-8,2,"#F0D392DD")
    end
    if gate then
        local p=sc.get(gate)
        sc.rect(p.x+2,p.y+4,2,p.h-8,"#F0B7B8CC")
        for y=p.y+10,p.y+p.h-8,12 do sc.rect(p.x+5,y,5,2,"#602F4CCC") end
    end
    for _,id in ipairs(controls) do
        local p=sc.get(id)
        sc.rect(p.x+2,p.y+2,p.w-4,2,colors.light)
        sc.rect(p.x+4,p.y+6,2,8,"#1A293BDD")
    end
end
return View
