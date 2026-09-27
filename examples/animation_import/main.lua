local Animation=require("shiny.animation")
local hero=require("content.animations").hero
local names={"idle","walk","once"}
local actors={}
return {
 title="ShinyCore / imported animation",width=320,height=180,gravity=0,ambient=1,
 map={tile_size=8,rows={"."},background="#101C2EFF"},
 init=function()
  for i,name in ipairs(names) do
   local clock=Animation.new(hero.clips);Animation.play(clock,name)
   actors[i]={clock=clock,id=sc.spawn{x=42+(i-1)*94,y=70,w=24,h=36,body=false,
    sprite="hero",frame_w=hero.frame_w,frame_h=hero.frame_h}}
  end
 end,
 update=function(dt)
  for _,a in ipairs(actors) do sc.set(a.id,{frame=Animation.update(a.clock,dt)}) end
  local a,b,c=actors[1],actors[2],actors[3]
  sc.debug.watch("animation",{idle=Animation.frame(a.clock),walk=Animation.frame(b.clock),
   once=Animation.frame(c.clock),done=c.clock.done})
 end,
 draw=function()
  sc.text("ASEPRITE / PNG + JSON",12,12,16,"#E6EDF7FF",true)
  sc.text("LOCAL ATLAS + LUA CLIPS",12,35,10,"#8CA4C4FF",true)
  for i,name in ipairs(names) do
   sc.rect(27+(i-1)*94,108,54,2,"#45677FFF",true)
   sc.text(name:upper(),34+(i-1)*94,120,12,"#FFCB77FF",true)
  end
  sc.text(actors[3].clock.done and "ONCE: COMPLETE" or "ONCE: PLAYING",12,156,11,"#66D9B0FF",true)
 end,
}
