local Controller=require('game.controller')
local Animation=require('game.animation')
local tr=require('game.locale')
local player,platform,controller,animation
return {
    title='ShinyCore / Starlight Workshop',width=384,height=224,gravity=600,ambient=0.8,
    map='workshop.tmj',
    init=function()
        player=sc.spawn({tag='player',x=24,y=160,w=10,h=16,sprite='assets/keeper.png',frame_w=12,frame_h=18,
            layer=2,body={type='dynamic',shape='capsule',friction=0,fixed_rotation=true},glow=40})
        sc.spawn({tag='crate',x=100,y=160,w=14,h=14,color='#DCA95E',body={type='dynamic',friction=0.2,fixed_rotation=false}})
        platform=sc.spawn({tag='platform',x=170,y=160,w=44,h=6,color='#719C90',body={type='kinematic',one_way=true}})
        sc.spawn({tag='slope',x=280,y=160,w=32,h=32,color='#709784',body={type='static',shape='polygon',vertices={0,32,32,0,32,32}}})
        controller=Controller.new(player)
        animation=Animation.new({idle={{frame=0,duration=0.2},loop=true},walk={{frame=0,duration=.12},{frame=1,duration=.12},loop=true}})
        Animation.play(animation,'idle')
        sc.state.set('visits',(sc.state.get('visits') or 0)+1)
        sc.audio.play('theme',{loop=true,volume=.05,fade=.3})
    end,
    update=function(dt)
        Controller.update(controller,dt)
        sc.set(platform,{vx=math.sin(sc.time())*20,vy=math.cos(sc.time())*-6})
        local p=sc.get(player)
        Animation.play(animation,math.abs(p.vx)>1 and 'walk' or 'idle')
        local frame=Animation.update(animation,dt); sc.set(player,{frame=frame})
        if sc.pressed('action') then
            sc.state.set('checkpoint',{room='main.lua',coins=sc.state.get('coins') or 0})
            assert(sc.save.write('checkpoint')); sc.audio.play('chime',{volume=.3})
        end
        if sc.pressed('up') then local ok,err=sc.save.load('checkpoint'); if not ok then sc.log(err) end end
        if p.x>330 then sc.state.set('coins',1); sc.scene('rooms/quiet.lua') end
    end,
    draw=function()
        sc.rect(8,8,368,76,'#10242BE8',true)
        sc.text(tr('zh','title'),18,13,16,'#F4D99C',true,{font='ui'})
        sc.text(tr('zh','move'),18,37,16,'#C2D8CF',true,{font='ui'})
        sc.text(tr('zh','save'),18,60,16,'#91B7AD',true,{font='ui'})
        sc.text(tr('zh','room'),310,168,16,'#F4D99C',false,{font='ui'})
    end,
}
