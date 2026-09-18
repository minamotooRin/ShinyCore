local Shell=require("shiny.shell")
local shell,player,platform,gems,collected,room
local function populate()
    room=sc.state.get("room") or 1
    player=sc.spawn({tag="player",x=24,y=130,w=9,h=14,body={type="dynamic",friction=0,fixed_rotation=true},color="#FFCB77FF"})
    platform=sc.spawn({tag="platform",x=280,y=135,w=40,h=8,body={type="kinematic",one_way=true},color="#66D9B0FF"})
    gems={}; collected=0
    for i=1,5 do gems[#gems+1]=sc.spawn({tag="gem",x=90+i*135,y=i%2==0 and 125 or 160,w=7,h=7,color="#70BFFFFF",solid=false}) end
    sc.camera(player)
end
local rows={}
for y=0,26 do
    local row={}
    for x=0,127 do
        row[#row+1]=(x==0 or x==127 or y>=24) and "#" or "."
        if y==19 and (x>=20 and x<=29 or x>=60 and x<=70 or x>=95 and x<=104) then row[#row]="=" end
    end
    rows[#rows+1]=table.concat(row)
end
return {
    title="ShinyCore / Crossing",width=384,height=216,gravity=550,ambient=1,
    map={tile_size=8,rows=rows,color="#263C58FF",accent="#648AACFF"},
    init=function()
        populate()
        shell=Shell.new("CROSSING","A/D move / Space jump / collect 5 lights per crossing")
        if room>1 then shell.mode="game" end
    end,
    update=function(dt)
        if not Shell.update(shell,dt) then return end
        local p=sc.get(player)
        local direction=(sc.input.key_down("d") and 1 or 0)-(sc.input.key_down("a") and 1 or 0)
        local patch={vx=direction*90}
        if p.grounded and sc.input.key_pressed("space") then patch.vy=-230 end
        sc.set(player,patch)
        sc.set(platform,{vx=math.cos(sc.time())*45})
        for i=#gems,1,-1 do
            local g=sc.get(gems[i])
            if math.abs(g.x-p.x)<12 and math.abs(g.y-p.y)<20 then
                sc.emit(g.x,g.y,16,"#70BFFFFF",24,.7); sc.destroy(gems[i]); table.remove(gems,i); collected=collected+1
            end
        end
        if p.x>950 and collected==5 then
            if room==3 then Shell.finish(shell,"All three crossings are restored.")
            else sc.state.set("room",room+1); sc.scene("main.lua") end
        end
        sc.debug.watch("crossing",{room=room,collected=collected,grounded=p.grounded})
    end,
    draw=function()
        sc.rect(0,0,384,25,"#101C2EE8",true)
        sc.text("CROSSING "..room.." / 3    lights "..collected.." / 5    Escape menu",8,8,11,"#E6EDF7FF",true)
        Shell.draw(shell)
    end,
}
