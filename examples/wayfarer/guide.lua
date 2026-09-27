-- Guidance reads only the quest and currently loaded herbs; it never reads chunks.
local Scenery=require("scenery")
local Guide={}
function Guide.target(stage,bag,cleared,player,herbs)
    if stage=="complete" then return nil end
    if stage=="meet" or (bag>=24 and cleared) then return {x=208,y=100,label="药师",kind="healer"} end
    if bag>=24 then return {x=328,y=128,label="路障",kind="road"} end
    local target,distance
    for _,herb in ipairs(herbs) do
        local d=(herb.x-player.x)^2+(herb.y-player.y)^2
        if not distance or d<distance then target=herb;distance=d end
    end
    if target then return {x=target.x,y=target.y,label="药草",kind="herb"} end
end
local function pin(x,y,color,size)
    sc.rect(299+x/1024*72-size/2,34+y/1024*72-size/2,size,size,color,true)
end
function Guide.draw(player,herbs,target,reach,use)
    -- A locator, not a reveal of unloaded objects. Up is north, road stays at y=128.
    sc.rect(293,28,86,101,"#101C2EEE",true)
    sc.rect(299,34,72,72,"#244438FF",true)
    sc.rect(299,43,72,2,"#AB9C6BFF",true)
    for _,prop in ipairs(Scenery.landmarks) do pin(prop.x+prop.w/2,prop.y+prop.h/2,"#8A9A9DFF",2) end
    for _,herb in ipairs(herbs) do pin(herb.x,herb.y,"#66D9B0FF",2) end
    pin(208,100,"#9EC5FFFF",3)
    pin(player.x,player.y,"#FFF0C9FF",4)
    sc.text("N",332,29,8,"#E6EDF7FF",true)
    sc.text(Scenery.region(player.x,player.y),299,110,12,"#CEDFFFFF",true,{font="ui"})
    if not target then
        sc.text("继续探索林地",8,174,12,"#CEDFFFFF",true,{font="ui"})
        return
    end
    local point=sc.camera.to_screen(target.x+3,target.y-8)
    local off=point.x<16 or point.x>280 or point.y<60 or point.y>168
    local x,y=math.max(16,math.min(280,point.x)),math.max(60,math.min(168,point.y))
    local near=math.abs(player.x-target.x)<(target.kind=="herb" and reach or 24)
        and math.abs(player.y-target.y)<(target.kind=="herb" and reach or 24)
    local label=target.label..(near and " ["..use.."]" or "")
    local width=sc.measure(label,12,"ui")
    local left=math.max(4,math.min(280-width,x-width/2))
    sc.rect(left-3,y-17,width+6,17,"#101C2EEE",true)
    sc.text(label,left,y-16,12,near and "#FFCB77FF" or "#CEDFFFFF",true,{font="ui"})
    local arrow=off and (point.x<16 and "<" or point.x>280 and ">" or point.y<60 and "^" or "v") or "v"
    sc.text(arrow,x-3,y,10,"#FFCB77FF",true)
end
return Guide
