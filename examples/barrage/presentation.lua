-- Cosmetic state only: no gameplay RNG, collision or progression changes.
local Animation=require('shiny.animation')
local View={}
function View.player_animation()
    local a=Animation.new{
        idle={loop=true,{frame=0,duration=.5}},
        walk={loop=true,{frame=1,duration=.125},{frame=2,duration=.125},{frame=3,duration=.125}},
        -- Atlas 4..7 is left-facing artwork; keep right-facing frames and let flip_x own direction.
        dash={{frame=1,duration=.05},{frame=2,duration=.05},{frame=3,duration=.05},{frame=2,duration=.05}},
    }
    Animation.play(a,'idle');return a
end
function View.animate_player(a,dt,moving,dashing)
    Animation.play(a,dashing and 'dash' or (moving and 'walk' or 'idle'))
    return Animation.update(a,dt)
end
function View.enemy_animation(kind)
    local rate=assert(({scout=6,runner=10,gunner=4,brute=5,guardian=4})[kind])
    local frames={loop=true}
    for i=0,3 do frames[#frames+1]={frame=i,duration=1/rate} end
    local a=Animation.new{hover=frames};Animation.play(a,'hover');return a
end
function View.animate_enemy(a,dt) return Animation.update(a,dt) end
local Challenge=require('challenge')
function View.new() return {labels={},hurt=0} end
function View.update(view,dt)
    view.hurt=math.max(0,view.hurt-dt)
    for i=#view.labels,1,-1 do
        local label=view.labels[i]; label.age=label.age+dt
        if label.age>=.6 then table.remove(view.labels,i) end
    end
end
function View.feedback(view,x,y,text,color)
    if #view.labels==16 then table.remove(view.labels,1) end
    view.labels[#view.labels+1]={x=x,y=y,text=text,color=color,age=0}
end
function View.hurt(view,x,y)
    view.hurt=.25
    View.feedback(view,x,y,'-1','#FF899A')
end
function View.enemy(enemy,body,spec,player)
    if enemy.health<spec.health then
        sc.rect(body.x,body.y-4,body.w,2,'#493044')
        sc.rect(body.x,body.y-4,body.w*math.max(0,enemy.health)/spec.health,2,'#FF899A')
    end
    if spec.period and enemy.fire<=.5 then
        -- Existing fire clock supplies the warning; rendering never advances it.
        local charge=1-math.max(0,enemy.fire)/.5
        local x,y=body.x+body.w/2,body.y+body.h/2
        local radius=body.w/2+3+(1-charge)*6
        for sign=-1,1,2 do
            sc.rect(x+sign*radius-1,y-1,3,3,'#FF899A')
            sc.rect(x-1,y+sign*radius-1,3,3,'#FF899A')
        end
        local dx,dy=player.x+player.w/2-x,player.y+player.h/2-y
        local length=math.sqrt(dx*dx+dy*dy)
        if length>0 then
            sc.circle(x+dx/length*(radius+4),y+dy/length*(radius+4),1+charge,'#FFE6AB')
        end
    end
end
function View.draw(view,run,cooldown,guardian,dash_key)
    for _,label in ipairs(view.labels) do
        local y=math.floor(math.max(36,label.y-12-label.age*15))
        local x=math.floor(math.max(4,math.min(360,label.x)))
        sc.text(label.text,x+1,y+1,10,'#111C2E')
        sc.text(label.text,x,y,10,label.color)
    end
    if view.hurt>0 then
        local color=view.hurt>.12 and '#FF819A99' or '#FF819A44'
        sc.rect(0,26,3,176,color,true);sc.rect(381,26,3,176,color,true)
        sc.rect(0,26,384,3,color,true);sc.rect(0,199,384,3,color,true)
    end
    sc.rect(0,0,384,26,'#111C2EF0',true)
    sc.text('WAVE '..run.wave..' / 6',8,7,11,'#E6EDF7',true)
    sc.text('HP',102,7,10,'#A4B8D4',true)
    for i=1,run.max_health do
        sc.rect(120+(i-1)*9,8,7,8,i<=run.health and (run.health<=2 and '#FF899A' or '#66D9B0') or '#304159',true)
    end
    sc.text('SCORE '..run.score,280,7,10,'#E6EDF7',true)
    sc.rect(8,23,368*math.min(1,run.time/Challenge.wave_seconds),2,'#66D9B0',true)
    if guardian then
        sc.rect(112,29,160,14,'#111C2EF0',true)
        sc.text('GUARDIAN',166,30,7,'#FFC98C',true)
        sc.rect(116,40,152,2,'#493044',true)
        sc.rect(116,40,152*guardian,2,'#FF899A',true)
    end
    sc.rect(0,202,384,14,'#111C2EF0',true)
    sc.text('DASH / '..(dash_key or 'SHIFT')..' / '..(cooldown==0 and 'READY' or string.format('%.1fs',cooldown)),8,204,9,'#A4B8D4',true)
    sc.text(math.max(0,math.ceil(Challenge.wave_seconds-run.time))..'s',270,204,9,'#A4B8D4',true)
    sc.rect(308,207,68,4,'#304159',true)
    sc.rect(308,207,68*(1-math.min(1,cooldown/2)),4,cooldown==0 and '#66D9B0' or '#FFCB77',true)
end
return View
