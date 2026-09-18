local Shell=require("shiny.shell")
local UI=require("shiny.ui")
local shell,player,herbs,bag,inventory,completed
local rows={}
for y=0,127 do rows[y+1]=string.rep((y==0 or y==127) and "#" or ".",128) end
-- Keep the authored map ordinary text and navigable.
for y=1,128 do rows[y]="#"..string.rep((y==1 or y==128) and "#" or ".",126).."#" end
return {
    title="ShinyCore / Wayfarer",width=384,height=216,gravity=0,ambient=1,
    map={tile_size=8,rows=rows,color="#244A3EFF",accent="#46745CFF"},
    init=function()
        player=sc.spawn({tag="player",x=160,y=100,w=8,h=12,color="#FFCB77FF",body=false,solid=false})
        herbs={}; bag=sc.state.get("herbs") or 0; completed=false
        for i=1,24 do herbs[#herbs+1]=sc.spawn({tag="herb",x=40+(i*137)%900,y=30+(i*193)%900,w=6,h=6,color="#66D9B0FF",solid=false}) end
        sc.camera(player)
        inventory=UI.new({id="inventory",kind="column",x=24,y=25,w=336,h=180,children={
            {id="caption",kind="label",text="WAYFARER / FIELD JOURNAL",h=24},
            {id="name",kind="input",value="Traveler",h=26,max_bytes=96},
            {id="items",kind="list",h=85,items={}},
            {id="close",kind="button",text="CLOSE [I]",h=24,on_click=function() inventory.open=false end},
        }})
        shell=Shell.new("WAYFARER","WASD travel / E gather / I inventory / gather 24 herbs")
    end,
    update=function(dt)
        if not Shell.update(shell,dt) then return end
        if sc.input.key_pressed("i") then inventory.open=not inventory.open end
        if inventory.open then UI.update(inventory,dt,384,216); return end
        local p=sc.get(player)
        local dx=(sc.input.key_down("d") and 1 or 0)-(sc.input.key_down("a") and 1 or 0)
        local dy=(sc.input.key_down("s") and 1 or 0)-(sc.input.key_down("w") and 1 or 0)
        sc.set(player,{x=math.max(8,math.min(1000,p.x+dx*85*dt)),y=math.max(8,math.min(1000,p.y+dy*85*dt))})
        if sc.input.key_pressed("e") then
            for i=#herbs,1,-1 do
                local h=sc.get(herbs[i])
                if math.abs(p.x-h.x)<20 and math.abs(p.y-h.y)<20 then
                    sc.destroy(herbs[i]); table.remove(herbs,i); bag=bag+1; sc.state.set("herbs",bag)
                    inventory.nodes.items.items[#inventory.nodes.items.items+1]="Moon herb #"..bag
                end
            end
        end
        if bag>=24 and not completed then completed=true; Shell.finish(shell,"The village has enough medicine. Thank you!") end
        sc.debug.watch("quest",{collected=bag,remaining=#herbs,complete=completed})
    end,
    draw=function()
        sc.rect(0,0,384,24,"#101C2EDD",true)
        sc.text("WAYFARER   herbs "..bag.." / 24    I inventory",8,7,11,"#E6EDF7FF",true)
        if inventory.open then UI.draw(inventory) end
        Shell.draw(shell)
    end,
}
