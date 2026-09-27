local function particle(x,y,color,blend,texture,size,life)
    local id=sc.particles.define({speed_min=0,speed_max=0,life_min=life or 60,life_max=life or 60,
        gravity=0,blend=blend,texture=texture,
        curve={{time=0,size=size,color=color},{time=1,size=size,color=color}}})
    sc.particles.burst(id,x,y,1)
end
return {title="ShinyCore / Particle materials",width=640,height=360,gravity=0,ambient=1,
init=function()
    particle(10,10,0xffffffff,"alpha",nil,5,.001)
    for i=0,2 do
        particle(70+i*180,125,0xff000080,i==0 and "alpha" or "additive",nil,65)
        particle(90+i*180,145,0x0000ff80,i==1 and "additive" or "alpha",nil,65)
    end
    local texture={resource="keeper",x=0,y=0,w=12,h=18}
    particle(95,245,0xffffffc0,"alpha",texture,54)
    particle(275,245,0xffffffc0,"additive",texture,54)
    particle(455,250,0x40d080ff,"alpha",nil,40)
end,
update=function() sc.debug.watch("materials",sc.particles.stats()) end,
draw=function()
    sc.rect(0,0,640,360,"#203040",false)
    sc.rect(540,250,30,30,"#FF000080",false)
    sc.text("PARTICLE TEXTURES + BLENDING",24,22,24,"#E8EFF8",true)
    sc.text("Alpha",70,92,16,"#FFFFFF",true)
    sc.text("Additive",250,92,16,"#FFFFFF",true)
    sc.text("Add then alpha",430,92,16,"#FFFFFF",true)
    sc.text("PNG alpha",70,220,14,"#FFFFFF",true)
    sc.text("PNG additive",250,220,14,"#FFFFFF",true)
    sc.text("White texture reset",430,220,14,"#FFFFFF",true)
    sc.text("Scene alpha",525,288,12,"#FFFFFF",true)
    sc.text("Stable creation order / atlas aspect ratio / no entity per particle",24,326,14,"#A8BDDA",true)
end}
