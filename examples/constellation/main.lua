local C=require("game.config")
local Server=require("game.server")
local Client=require("game.client")
local address={table.unpack(C.address)}
local selected,notice=1,"Host one window, then join from three others."
local function begin(host)
    if not sc.net.available then notice="Use a SHINY_NETWORK=ON build."; return end
    local ip=table.concat(address,".")
    local h,err
    if host then h,err=sc.net.host("0.0.0.0",C.port,8) else h,err=sc.net.join(ip,C.port) end
    if not h then notice=err; return end
    local ok,problem=h:persist("coop")
    if not ok then h:close(); notice=problem; return end
    local state=host and Server.new() or Client.state(ip,C.port)
    ok,problem=h:state(state)
    if not ok then h:close(); notice=problem; return end
    if host then sc.log("CONSTELLATION listening "..h:port()) end
    sc.scene(C.paths[1])
end
return {title="Constellation / Four shared signals",width=720,height=400,gravity=0,ambient=1,
update=function()
    if sc.key_pressed("left") then selected=(selected-2)%4+1 end
    if sc.key_pressed("right") then selected=selected%4+1 end
    if sc.key_pressed("up") then address[selected]=(address[selected]+1)%256 end
    if sc.key_pressed("down") then address[selected]=(address[selected]-1)%256 end
    if sc.key_pressed("h") then begin(true) elseif sc.key_pressed("j") then begin(false) end
    if sc.key_pressed("escape") then sc.app.quit() end
end,draw=function()
    sc.rect(0,0,720,400,"#0D192B",true)
    sc.text("C O N S T E L L A T I O N",24,30,26,"#EAF1FF",true)
    sc.text("FOUR PLAYERS / TWO ROOMS / ONE SHARED SIGNAL",24,78,16,"#9FB6D2",true)
    for i=1,4 do sc.circle(180+(i-1)*120,165,22,C.colors[i],true) end
    sc.text("H: host   J: join   Escape: quit",24,228,20,"#79DFCA",true)
    sc.text("Server: "..table.concat(address,".")..":"..C.port,24,272,18,"#EAF1FF",true)
    sc.text("Left/right: select address octet "..selected.."   Up/down: edit",24,302,14,"#9FB6D2",true)
    sc.text(notice,24,355,14,"#FFD18A",true)
end}
