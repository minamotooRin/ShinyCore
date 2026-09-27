local player, zoom, rotation = nil, 1, 0
local interpolate=true
return {width=480,height=300,gravity=0,ambient=.8,
    init=function()
        sc.presentation.interpolate(true)
        sc.camera.set{bounds={x=-600,y=-400,w=1200,h=800},pixel_snap=false}
        player=sc.spawn{x=0,y=0,w=20,h=20,color="#FFD080",glow=120}
        sc.camera.follow(player)
    end,
    update=function(dt)
        local p=sc.get(player)
        local dx=(sc.key_down("d") and 1 or 0)-(sc.key_down("a") and 1 or 0)
        local dy=(sc.key_down("s") and 1 or 0)-(sc.key_down("w") and 1 or 0)
        sc.set(player,{x=p.x+dx*120*dt,y=p.y+dy*120*dt})
        if sc.key_pressed("q") then rotation=rotation-.2 end
        if sc.key_pressed("e") then rotation=rotation+.2 end
        if sc.key_pressed("up") then zoom=math.min(4,zoom+.25) end
        if sc.key_pressed("down") then zoom=math.max(.5,zoom-.25) end
        sc.camera.set{zoom=zoom,rotation=rotation}
        if sc.key_pressed("space") then sc.camera.shake(8,.4,42) end
        if sc.key_pressed("i") then
            interpolate=not interpolate
            sc.presentation.interpolate(interpolate)
        end
        if sc.key_pressed("t") then
            sc.set(player,{x=p.x+100})
            sc.presentation.snap(player)
            sc.camera.follow(player)
            sc.presentation.snap_camera()
        end
    end,
    draw=function()
        local p=sc.presentation.pose(player)
        sc.text("Player",p.x-6,p.y-14,10,"#FFD080")
        for x=-600,600,50 do sc.rect(x,-400,1,800,"#384857") end
        for y=-400,400,50 do sc.rect(-600,y,1200,1,"#384857") end
        local mx,my,inside=sc.input.mouse()
        if inside then
            local p=sc.camera.to_world(mx,my)
            sc.circle(p.x,p.y,4,"#FF6688")
        end
        sc.text("WASD move  Q/E rotate  arrows zoom  Space shake",10,10,10,"#FFFFFF",true)
        sc.text("I interpolation: "..tostring(interpolate).."  T teleport",10,26,10,"#FFFFFF",true)
    end
}
