local Particles=require("shiny.particles")
local colors={0x77E8C8CC,0xF4BA78CC,0x8EB5FFCC}
local emitters={}
return {title="ShinyCore / Particle columns",width=640,height=360,gravity=160,ambient=1,
init=function()
    for i,color in ipairs(colors) do
        emitters[i]=Particles.new({speed_min=25,speed_max=90,life_min=.8,life_max=1.2,
            angle_min=-1.9,angle_max=-1.2,gravity=.3,
            curve={{time=0,size=2,color=0xFFFFFFFF},{time=.3,size=6,color=color},
                   {time=1,size=0,color=color&0xFFFFFF00}}},120)
    end
end,
update=function(dt)
    for i,emitter in ipairs(emitters) do Particles.update(emitter,dt,i*170-20,250) end
    sc.debug.watch("gameplay_rng",sc.random(1,1000000))
    sc.debug.watch("particles",sc.particles.stats())
end,
draw=function()
    sc.text("PARTICLE EMITTERS",24,22,24,"#E8EFF8",true)
    sc.text("Speed + lifetime ranges / size + RGBA curves / stable alpha order",24,62,14,"#A8BDDA",true)
    sc.text("Three emitters / 120 particles per second each / fixed 2,048 capacity",24,318,14,"#A8BDDA",true)
end}
