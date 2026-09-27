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
return View
