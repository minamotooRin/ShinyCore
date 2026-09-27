local material,warm,time,status,bound
return {title="ShinyCore / Material bindings",width=640,height=360,gravity=0,ambient=1,map="room.tmj",
init=function()
    material=sc.material.create{shader="tint",uniforms={
        tint={type="vec3",value={.3,.9,.7}},strength={type="float",value=.5}}}
    warm=sc.material.create{shader="tint",uniforms={
        tint={type="vec3",value={1,.45,.35}},strength={type="float",value=.8}}}
    sc.material.bind_image("keeper",material);bound=true
    sc.camera.set{bounds=false}
    sc.spawn{x=340,y=80,w=72,h=108,sprite="keeper",frame_w=12,frame_h=18}
    local plain=sc.spawn{x=490,y=80,w=72,h=108,sprite="keeper",frame_w=12,frame_h=18}
    sc.material.bind_entity(plain,false)
    local shape=sc.spawn{x=358,y=245,w=64,h=54,color="#FFFFFF"}
    sc.material.bind_entity(shape,warm)
    sc.projectiles.configure(8)
    local sprite=sc.projectiles.sprite("keeper",0,0,12,18,36,54)
    sc.projectiles.spawn{{x=76,y=272,sprite=sprite,life=60,terrain=false,mask=0},
        {x=92,y=280,sprite=sprite,color=0xFFA08088,life=60,terrain=false,mask=0}}
    local emitter=sc.particles.define{speed_min=0,speed_max=0,life_min=60,life_max=60,gravity=0,
        texture={resource="keeper",x=0,y=0,w=12,h=18},
        curve={{time=0,size=54,color=0xffffffff},{time=1,size=54,color=0xffffffff}}}
    sc.particles.burst(emitter,220,245,1)
    time=0;status=sc.material.info(material)
end,update=function(dt)
    time=time+dt
    sc.material.set(material,{strength=.5+.45*math.sin(time*2)})
    if sc.input.key_pressed("b") then bound=not bound;sc.material.bind_image("keeper",bound and material or 0) end
    if sc.input.key_pressed("r") then
        local ok,error=pcall(sc.material.reload,material)
        if not ok then sc.debug.watch("reload_error",error) end
    end
    status=sc.material.info(material)
    sc.debug.watch("material",status);sc.debug.watch("bindings",sc.material.capacity())
end,draw=function()
    sc.image("keeper",40,80,72,108,{source_w=12,source_h=18,material=false})
    sc.image("keeper",190,80,72,108,{source_w=12,source_h=18,material=warm})
    sc.text("Material bindings / B: toggle image default / R: reload",18,18,17,"#E6EDF7",true)
    for i,label in ipairs({"Image original","Image override","Sprite default","Sprite builtin"}) do
        sc.text(label,36+(i-1)*150,54,13,"#90B1CF",true)
    end
    for i,label in ipairs({"Projectiles","Particles","Shape override","Tiles"}) do
        sc.text(label,36+(i-1)*150,216,13,"#90B1CF",true)
    end
    sc.text("Image binding: "..tostring(bound).." | GPU status: "..status.status,18,336,13,"#90B1CF",true)
end}
