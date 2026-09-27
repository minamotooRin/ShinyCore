-- Game rules remain ordinary Lua data, independent of UI and native handles.
local Quest={}
Quest.equipment={trail={label="轻行靴：移动快",speed=85,reach=20},field={label="采药靴：采集远",speed=68,reach=32}}
function Quest.in_reach(player,herb,reach)
    return math.abs(player.x-herb.x)<reach and math.abs(player.y-herb.y)<reach
end
function Quest.objective(stage,herbs,cleared,use)
    use=use or "E"
    if stage=="meet" then return "按 "..use.." 与村口药师交谈。" end
    if stage=="complete" then return "药草已送达，村庄平安。" end
    if herbs<24 then return "采集月光草："..herbs.." / 24" end
    if not cleared then return "按 "..use.." 清理路障，坐标 (328,128)。" end
    return "返回村口，按 "..use.." 交付药草。"
end
function Quest.talk(stage,herbs,cleared,use)
    if stage=="meet" then
        return "gather","村里急需二十四株月光草。请清理东边小路的石头，采齐后回来找我。日志中可以更换靴子。",false
    end
    if herbs>=24 and cleared and stage~="complete" then
        return "complete","药草齐了，道路也畅通了。今晚大家终于能安心休息。谢谢你，旅人。",true
    end
    return stage,Quest.objective(stage,herbs,cleared,use),false
end
return Quest
