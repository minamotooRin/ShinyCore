local Tween=require('shiny.tween')
local upper,lower,timeline,paused
local function restart()
    upper,lower={x=40},{x=40};paused=false
    timeline=Tween.sequence{
        Tween.parallel{Tween.new(upper,{x=320},1,'smooth'),Tween.new(lower,{x=320},1.5,'smooth')},
        Tween.delay(.3),
        Tween.parallel{Tween.new(upper,{x=40},.8,'smooth'),Tween.new(lower,{x=40},.8,'smooth')},
    }
end
return {width=384,height=216,gravity=0,ambient=1,
    map={tile_size=8,rows={'.'},background='#111C2EFF'},
    init=restart,
    update=function(dt)
        if sc.input.key_pressed('r') then restart() end
        if sc.input.key_pressed('space') then paused=not paused end
        if sc.input.key_pressed('c') then Tween.cancel(timeline) end
        if not paused then Tween.update(timeline,dt) end
        sc.debug.watch('timeline',{upper=upper.x,lower=lower.x,stage=timeline.index,
            done=timeline.done,cancelled=timeline.cancelled,paused=paused})
    end,
    draw=function()
        sc.text('SEQUENCE + PARALLEL',20,16,20,'#E6EDF7',true)
        sc.text('MOVE TOGETHER / WAIT / RETURN',20,44,10,'#A4B8D4',true)
        sc.text('1.0s',20,72,10,'#66D9B0',true)
        sc.text('1.5s',20,114,10,'#FFC98C',true)
        for _,y in ipairs({94,136}) do sc.rect(40,y,296,2,'#304159',true) end
        sc.rect(upper.x,86,16,16,'#66D9B0',true)
        sc.rect(lower.x,128,16,16,'#FFC98C',true)
        local status=timeline.cancelled and 'CANCELLED' or (timeline.done and 'COMPLETE' or (paused and 'PAUSED' or 'RUNNING'))
        sc.text(status..' / STAGE '..math.min(3,timeline.index),20,160,12,'#E6EDF7',true)
        sc.text('SPACE pause   C cancel   R restart',20,194,10,'#A4B8D4',true)
    end}
