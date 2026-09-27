local Tiles=require("shiny.stream_tiles")
local view,prepared,plaque
return {
 title="ShinyCore / TSX import",width=320,height=180,gravity=0,ambient=1,
 map={tile_size=8,rows={"."},background="#14243AFF"},
 init=function()
  sc.stream.open("content/map-court/index.json")
  view=Tiles.new(sc.stream.metadata(),{["assets/ground.png"]="ground",["assets/flower.png"]="flower"})
  sc.stream.request(0,0,0)
 end,
 update=function(dt)
  local chunk=sc.stream.get(0,0)
  if chunk and not prepared then
   prepared=Tiles.prepare(view,{chunk})
   plaque=chunk.objects[1]
   assert(plaque and plaque.persistent_id=="markers:1" and plaque.gid==13)
  end
  Tiles.update(view,dt)
  sc.debug.watch("tsx",{ready=prepared~=nil,frames=view.time,template=plaque and plaque.persistent_id})
 end,
 draw=function()
  if prepared then Tiles.draw(view,prepared) end
  if plaque then
   sc.image("flower",plaque.x,plaque.y-24,18,24,{layer=10})
   sc.text("TX",plaque.x+20,plaque.y-18,10,"#FFCB77FF",true)
  end
  sc.text("TILED JSON + EXTERNAL TSX",12,4,12,"#E6EDF7FF",true)
  sc.text("ANIMATED ATLAS / IMAGE COLLECTION",12,160,10,"#FFCB77FF",true)
 end,
}
