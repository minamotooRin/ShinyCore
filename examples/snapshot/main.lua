-- Offline transport illustration. Actual UDP fault tests live in tests/test_net_proxy.py.
local S=require("shiny.snapshot")
local timeline=S.new(32,1)
local output=S.output(timeline)
local pending={}
local frame,count,status,latest_x,truth=0,0,"empty",100,100
local function position(tick) return 360+240*math.sin(tick/60) end
return {title="ShinyCore / Snapshot interpolation",width=720,height=400,gravity=0,ambient=1,
update=function()
    truth=position(frame)
    -- 20 Hz sender, 4..6 ticks of delay and one skipped packet in seven.
    -- Stop sending for one second in each six-second cycle.
    if frame%3==0 and (frame%360<180 or frame%360>=240) then
        local sequence=frame//3
        if sequence%7~=0 then
            pending[#pending+1]={sequence=sequence,tick=frame,due=frame+4+sequence%3,x=truth}
        end
    end
    for i=#pending,1,-1 do
        local packet=pending[i]
        if packet.due<=frame then
            if S.push(timeline,packet.sequence,packet.tick,{{id=1,x=packet.x,y=0}}) then latest_x=packet.x end
            table.remove(pending,i)
        end
    end
    count,status=S.sample(timeline,math.max(0,frame-12),output)
    sc.debug.watch("snapshot",{frame=frame,status=status,truth=truth,latest=latest_x,display=count>0 and output[1].x or 0})
    frame=frame+1
end,
draw=function()
    sc.rect(0,0,720,400,"#101B2D",true)
    sc.text("SNAPSHOT INTERPOLATION",24,22,26,"#E8EFF8",true)
    sc.text("20 Hz snapshots / 60 Hz display / 200 ms presentation delay",24,62,16,"#A8BDDA",true)
    local rows={{"Authority",truth,130,"#F4C88A"},{"Last packet",latest_x,205,"#EB8EA1"},
                {"Interpolated",count>0 and output[1].x or 100,280,"#7EE8D0"}}
    for _,row in ipairs(rows) do
        sc.text(row[1],24,row[3]-27,14,row[4],true)
        sc.rect(100,row[3],520,2,"#34485D",true)
        sc.circle(row[2],row[3],9,row[4],true)
    end
    sc.text("Delay + dropped packets; one-second outage every six seconds.",24,332,15,"#A8BDDA",true)
    sc.text("Timeline: "..status.." / positions hold when snapshots stop",24,358,15,"#7EE8D0",true)
end}
