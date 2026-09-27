-- Decorative props are local atlas regions; terrain remains the collision source.
local World=require("shiny.stream_world")
local Scenery={}
local regions={"村口林地","日照草甸","南部苔林","古石林地"}
local sprites={
    camp={80,100,615,515},sign={764,97,390,540},
    stone={62,663,560,497},log={660,850,555,326},
}
Scenery.landmarks={
    {kind="camp",x=168,y=28,w=86,h=72},
    {kind="sign",x=350,y=78,w=27,h=38},
    {kind="sign",x=638,y=88,w=24,h=34},
    {kind="stone",x=534,y=310,w=44,h=39},
    {kind="stone",x=764,y=766,w=44,h=39},
    {kind="log",x=265,y=607,w=58,h=34},
    {kind="log",x=816,y=685,w=58,h=34},
}
function Scenery.region(x,y)
    return regions[1+(x>=512 and 1 or 0)+(y>=512 and 2 or 0)]
end
function Scenery.draw(world)
    local camera=sc.camera.read()
    for _,prop in ipairs(Scenery.landmarks) do
        if prop.x+prop.w>=camera.x and prop.x<=camera.x+384
            and prop.y+prop.h>=camera.y and prop.y<=camera.y+216 and World.contains(world,prop) then
            local source=sprites[prop.kind]
            sc.image("landmarks",prop.x,prop.y,prop.w,prop.h,{source_x=source[1],source_y=source[2],
                source_w=source[3],source_h=source[4],layer=0})
        end
    end
end
return Scenery
