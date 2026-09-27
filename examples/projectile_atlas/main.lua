return {title="ShinyCore / Projectile atlas",width=640,height=360,gravity=0,ambient=1,
init=function()
    sc.projectiles.configure(64)
    local batch={}
    for i=0,7 do
        local sprite=sc.projectiles.sprite("keeper",i*12,0,12,18,36,54)
        batch[#batch+1]={x=60+i*74,y=130,sprite=sprite,life=60,terrain=false,mask=0}
    end
    -- Removing the first dense slot must not put the last bullet behind older ones.
    batch[#batch+1]={x=30,y=240,radius=12,life=.02,terrain=false,mask=0}
    batch[#batch+1]={x=100,y=260,radius=30,color=0xff6060b0,life=60,terrain=false,mask=0}
    batch[#batch+1]={x=125,y=260,radius=30,color=0x60aaffb0,life=60,terrain=false,mask=0}
    batch[#batch+1]={x=320,y=260,sprite=1,color=0x60ffe0b0,life=60,terrain=false,mask=0}
    batch[#batch+1]={x=340,y=260,sprite=1,color=0xffa060b0,life=60,terrain=false,mask=0}
    sc.projectiles.spawn(batch)
end,
update=function()
    sc.debug.watch("atlas",{count=sc.projectiles.count(),hits=#sc.projectiles.hits()})
end,
draw=function()
    sc.text("PROJECTILE ATLAS",24,20,24,"#E8EFF8",true)
    sc.text("Eight source regions / 3x display scale / one texture",24,56,16,"#A8BDDA",true)
    sc.text("RGBA quads",60,205,16,"#A8BDDA",true)
    sc.text("Tint + alpha",280,205,16,"#A8BDDA",true)
    sc.text("Stable creation order survives removal. Collision radius is independent.",24,326,14,"#A8BDDA",true)
end}
