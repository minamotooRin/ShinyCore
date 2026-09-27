local lamp,box,decoration,time,mode,stats,cast
local function quality(soft)
    sc.lighting.configure{softness=soft and 8 or 0,samples=soft and 4 or 1}
    mode=soft and "Soft / 4 samples" or "Hard / 1 sample"
end
return {title="ShinyCore / Geometry shadows",width=480,height=270,gravity=0,ambient=.12,
map={tile_size=30,background="#65758A",color="#3B4D61",accent="#71859A",
    rows={"################","#..............#","#..............#",
    "#..............#","#..............#","#..............#","#..............#",
    "#..............#","################"}},
init=function()
    time=0;quality(false);cast=true
    lamp=sc.spawn{x=88,y=112,w=16,h=16,color="#FFF0BA",body={type="kinematic",shape="circle"}}
    box=sc.spawn{x=208,y=102,w=18,h=62,color="#92CDA5",body={type="kinematic"}}
    decoration=sc.spawn{x=145,y=66,w=18,h=38,color="#EC99A5",angle=.35}
    sc.lighting.occluder(decoration,"bounds")
    sc.spawn{x=322,y=134,w=70,h=22,color="#829DC4",body={type="static",shape="capsule"}}
    sc.spawn{x=150,y=190,w=50,h=30,color="#DBA977",body={type="static",shape="polygon",vertices={0,30,25,0,50,30}}}
    sc.spawn{x=365,y=58,w=46,h=46,color="#B8A4DE",body={type="static",shapes={
        {shape="box",x=0,y=0,w=12,h=46},{shape="box",x=34,y=0,w=12,h=46}}}}
    stats=sc.lighting.stats()
end,update=function(dt)
    time=time+dt
    local x=(sc.input.key_down("right") and 1 or 0)-(sc.input.key_down("left") and 1 or 0)
    local y=(sc.input.key_down("down") and 1 or 0)-(sc.input.key_down("up") and 1 or 0)
    local p=sc.get(lamp)
    sc.set(lamp,{x=math.max(34,math.min(430,p.x+x*100*dt)),y=math.max(34,math.min(216,p.y+y*100*dt))})
    sc.set(box,{angle=math.sin(time*.8)*.8})
    if sc.input.key_pressed("s") then quality(true) end
    if sc.input.key_pressed("h") then quality(false) end
    if sc.input.key_pressed("o") then
        cast=not cast;sc.lighting.occluder(decoration,cast and "bounds" or "none")
    end
    stats=sc.lighting.stats()
    sc.debug.watch("lighting",stats);sc.debug.watch("mode",mode)
end,draw=function()
    local p=sc.get(lamp)
    sc.lighting.point{x=p.x+p.w/2,y=p.y+p.h/2,radius=220,color="#FFF0BA",ignore=lamp}
    sc.lighting.point{x=365,y=80,radius=100,color="#829DFF",intensity=.35,shadows=false}
    sc.text("Geometry shadows",14,8,18,"#EDF2FC",true)
    sc.text("Arrows: light | H/S: quality | O: decor shadow",14,242,12,"#EDF2FC",true)
    sc.text(mode,14,28,12,"#B7CBE5",true)
end}
