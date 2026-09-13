local Controller = {}
function Controller.new(id) return {id=id,coyote=0,buffer=0} end
function Controller.update(c,dt)
    local p=sc.get(c.id)
    c.coyote=p.grounded and 0.1 or math.max(0,c.coyote-dt)
    c.buffer=sc.pressed('jump') and 0.12 or math.max(0,c.buffer-dt)
    local axis=(sc.down('right') and 1 or 0)-(sc.down('left') and 1 or 0)
    local vx,vy=axis*90,p.vy
    if p.grounded then
        if p.support~=0 then vx=vx+sc.get(p.support).vx end
        if axis~=0 and p.normal_y<-.5 then vy=-vx*p.normal_x/p.normal_y end
    end
    if sc.down('down') and sc.pressed('jump') then
        sc.physics.drop(c.id,0.25); vy=50; c.buffer=0
    elseif c.buffer>0 and c.coyote>0 then
        vy=-220; c.buffer=0; c.coyote=0
    end
    sc.set(c.id,{vx=vx,vy=vy,flip_x=axis<0})
end
return Controller
