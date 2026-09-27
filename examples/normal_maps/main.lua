local time,rotating,height
return {title="ShinyCore / Normal maps",width=480,height=300,gravity=0,ambient=.18,map="room.tmj",
init=function()
    time=0;height=48
    sc.lighting.normal("atlas","normal")
    rotating=sc.spawn{x=190,y=40,w=64,h=64,sprite="atlas",frame_w=64,frame_h=64,frame=1,color="#FFFFFFFF"}
    sc.spawn{x=292,y=40,w=64,h=64,sprite="atlas",frame_w=64,frame_h=64,frame=1,flip_x=true,color="#FFFFFFFF"}
end,update=function(dt)
    time=time+dt;sc.set(rotating,{angle=time*.6})
    if sc.input.key_pressed("h") then height=height==48 and 128 or 48 end
    sc.debug.watch("normal_maps",sc.lighting.stats().normal_maps);sc.debug.watch("height",height)
end,draw=function()
    sc.image("atlas",70,40,64,64,{source_x=0,source_y=0,source_w=64,source_h=64})
    sc.image("atlas",384,40,64,64,{source_x=64,source_y=0,source_w=64,source_h=64,diagonal=true,flip_y=true})
    sc.lighting.point{x=240+math.sin(time*.7)*160,y=100+math.cos(time*.9)*70,radius=320,height=height,color="#FFEACD",shadows=false}
    sc.lighting.point{x=390,y=160,radius=180,height=64,intensity=.4,color="#739DFF",shadows=false}
    sc.text("Normal maps / moving colored lights",12,8,17,"#E7EBF5",true)
    sc.text("Sprites: rotation + flips | Tiles: GID transforms",12,222,13,"#BDCDE4",true)
    sc.text("H: light height "..height.." | source normals: +Y down",12,246,13,"#BDCDE4",true)
end}
