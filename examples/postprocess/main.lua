local effects,time,mode={},0,"Bloom + grade"
local function create(shader,uniforms)
    return sc.material.create{shader=shader,postprocess=true,uniforms=uniforms}
end
local function select(name,passes)
    mode=name; sc.material.postprocess(passes)
end
return {title="ShinyCore / Postprocess",width=480,height=270,gravity=0,ambient=1,init=function()
    effects.x=create("bloom_x",{threshold={type="float",value=.5}})
    effects.y=create("bloom_y")
    effects.mix=create("bloom_mix",{strength={type="float",value=1.2}})
    effects.grade=create("grade",{gain={type="vec3",value={1.08,.96,.88}},gamma={type="float",value=.9}})
    effects.warp=create("warp",{time={type="float",value=0},amplitude={type="float",value=.015}})
    select("Bloom + grade",{effects.x,effects.y,effects.mix,effects.grade})
end,update=function(dt)
    time=time+dt;sc.material.set(effects.warp,{time=time})
    if sc.input.key_pressed("b") then select("Bloom",{effects.x,effects.y,effects.mix}) end
    if sc.input.key_pressed("g") then select("Grade",{effects.grade}) end
    if sc.input.key_pressed("w") then select("Warp",{effects.warp}) end
    if sc.input.key_pressed("c") then select("Bloom + grade",{effects.x,effects.y,effects.mix,effects.grade}) end
    if sc.input.key_pressed("o") then select("Off",{}) end
    sc.debug.watch("pipeline",sc.material.pipeline());sc.debug.watch("mode",mode)
end,draw=function()
    sc.rect(0,0,480,270,"#101825")
    for i=0,9 do sc.rect(i*48,55,2,150,"#273850") end
    sc.image("keeper",192,58,96,144,{source_w=12,source_h=18})
    sc.circle(86+math.sin(time)*24,125,16,"#FFD996")
    sc.circle(390-math.sin(time)*24,125,16,"#88E5FF")
    sc.rect(0,0,480,45,"#101825",true)
    sc.text(mode,18,12,20,"#E6EDF7",true)
    sc.text("B bloom / G grade / W warp / C combined / O off",18,235,13,"#B1C4DF",true)
    sc.text("UI stays sharp; only the world is postprocessed",18,252,12,"#7F9AB8",true)
end}
