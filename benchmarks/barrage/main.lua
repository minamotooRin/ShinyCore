-- Fixed combined load. Surplus covers deaths between replenishment and profiling.
local enemies,reset,damage={},{},{}
local particle,flow
local hits_total=0
local goals={{8,8},{119,8},{119,63},{8,63}}
local rows={}
for i=1,72 do rows[i]=string.rep('.',128) end
local function replenish_bullets()
    local missing=math.min(2048,24000-sc.projectiles.count())
    while missing>0 do
        local count=math.min(missing,128)
        local batch={}
        for i=1,count do
            batch[i]={x=sc.random(30,1620),y=sc.random(30,1030),vx=180,
                radius=1,life=sc.random(.75,1.5),terrain=false,mask=1,color=0x88BBFFB0}
        end
        sc.projectiles.spawn(batch)
        missing=missing-count
    end
end
local function replenish_particles()
    local missing=math.min(2048,21000-sc.particles.stats().used)
    while missing>0 do
        local count=math.min(missing,128)
        sc.particles.burst(particle,sc.random(160,1760),sc.random(160,920),count)
        missing=missing-count
    end
end
return {title="ShinyCore / Combined barrage benchmark",width=1920,height=1080,gravity=0,ambient=1,
map={tile_size=15,rows=rows},
init=function()
    sc.projectiles.configure()
    particle=sc.particles.define({speed_min=20,speed_max=40,life_min=1,life_max=3,gravity=0,
        curve={{time=0,size=2,color=0xFFB870B0},{time=1,size=1,color=0xFFB87020}}})
    for i=0,1999 do
        local x,y=24+(i%80)*23,30+(i//80)*40
        local id=sc.spawn{x=x,y=y,w=6,h=6,body=false,solid=true,color="#77D5B0"}
        enemies[i+1]=id; reset[i+1]={id=id,patch={x=x,y=y}}
    end
end,
update=function()
    local tick=sc.tick()
    if tick%480==0 then sc.set_many(reset) end
    if tick%120==0 then
        local target=goals[(tick//120)%4+1]
        local status
        flow,status=sc.navigation.flow(target[1],target[2],16384)
        assert(status=='ok','benchmark flow field incomplete')
    end
    assert(sc.navigation.steer(flow,enemies,12)==2000)
    for _,hit in ipairs(sc.projectiles.hits()) do
        assert(hit.target~=0,'benchmark expects real enemy hits')
        damage[hit.target]=(damage[hit.target] or 0)+1
        hits_total=hits_total+1
    end
    if tick>=120 then
        assert(sc.projectiles.count()>=20000,'benchmark projectile load fell below 20000')
        assert(sc.particles.stats().used>=20000,'benchmark particle load fell below 20000')
    end
    replenish_bullets()
    replenish_particles()
    if tick%60==0 then sc.debug.watch('load',{enemies=#enemies,projectiles=sc.projectiles.count(),
        particles=sc.particles.stats().used,hits=hits_total}) end
end,
draw=function()
    sc.text('COMBINED LOAD: 20,000+ PROJECTILES / 2,000 MOVING ENEMIES / 20,000+ PARTICLES',16,12,18,'#FFFFFF',true)
end}
