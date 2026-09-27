-- Ephemeral visuals: no saved handles, random draws or gameplay mutations.
local Animation=require("shiny.animation")
local View={}
function View.actor()
    local clock=Animation.new{
        idle={{frame=0,duration=.5},loop=true},
        walk={{frame=1,duration=.125},{frame=2,duration=.125},{frame=3,duration=.125},loop=true},
    }
    Animation.play(clock,"idle")
    return {clock=clock,left=false}
end
function View.animate(actor,vx,vy,dt)
    if vx~=0 then actor.left=vx<0 end
    Animation.play(actor.clock,(vx~=0 or vy~=0) and "walk" or "idle")
    local frame=Animation.update(actor.clock,dt)
    return {frame=frame,flip_x=actor.left}
end
function View.feedback() return {items={},count=0,remaining=0} end
function View.advance(feedback,dt)
    feedback.remaining=math.max(0,feedback.remaining-dt)
    for i=#feedback.items,1,-1 do
        local item=feedback.items[i];item.age=item.age+dt
        if item.age>=.8 then table.remove(feedback.items,i) end
    end
end
function View.pickup(feedback,x,y)
    if #feedback.items==8 then table.remove(feedback.items,1) end
    feedback.items[#feedback.items+1]={x=x,y=y,age=0}
    feedback.count=(feedback.remaining>0 and feedback.count or 0)+1
    feedback.remaining=1.25
end
function View.focus(herb)
    local x,y,w,h=herb.x-2,herb.y-2,herb.w+4,herb.h+4
    for _,dx in ipairs{0,w-3} do
        sc.rect(x+dx,y,3,1,"#FFCB77FF");sc.rect(x+dx,y+h-1,3,1,"#FFCB77FF")
    end
end
function View.draw(feedback)
    for _,item in ipairs(feedback.items) do
        sc.text("+1",item.x-3,item.y-12-item.age*12,12,"#A4FFE0FF",false,{font="ui"})
    end
    if feedback.remaining>0 then
        local text="月光草 +"..feedback.count
        local width=sc.measure(text,16,"ui")
        sc.rect(8,28,width+12,22,"#101C2EEE",true)
        sc.text(text,14,30,16,"#A4FFE0FF",true,{font="ui"})
    end
end
return View
