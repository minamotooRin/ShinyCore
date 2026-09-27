-- Read-only presentation of campaign progress; rules and saves stay in campaign.lua.
local Campaign=require('campaign')
local levels=require('levels')
local Guide={}
local order={1,3,2}
function Guide.current(room,stage,x,crate_x,ferry_x,use)
    use=use or 'E'
    local level,count=levels[room],Campaign.count(stage)
    if not stage.open then
        if room==1 and count>=2 then
            return {text='Two lights ready. Pull the lever ['..use..'].',x=520,y=174,label='LEVER'}
        elseif room==2 then
            return {text='Push the crate onto the amber pressure plate.',x=crate_x or 300,y=176,label='PUSH CRATE'}
        elseif room==3 then
            local switch=level.controls[order[stage.sequence+1]]
            return {text=string.format('Align WEST > EAST > CENTER. Signal %d/3.',stage.sequence),
                x=switch[1],y=switch[2],label=switch[3]..' ['..use..']'}
        end
    elseif count==5 then
        return {text=room==3 and 'All lights ready. Reach the beacon.' or 'All lights ready. Reach the next crossing.',
            x=1216,y=148,label=room==3 and 'BEACON' or 'EXIT'}
    end
    local target,distance
    for i,p in ipairs(level.lights) do
        if not stage.lights['light.'..i] and (stage.open or i<=2) and (not distance or math.abs(p[1]-x)<distance) then
            target=p;distance=math.abs(p[1]-x)
        end
    end
    local text=stage.open and ('Gather the remaining lights. '..count..'/5 collected.') or
        ('Gather two lights for the lever. '..count..'/2 collected.')
    if room==1 and stage.open and x>=500 and x<696 and target[1]>696 then
        return {text='Jump onto the ferry and ride across the water.',x=ferry_x or 600,y=160,label='FERRY'}
    end
    return {text=text,x=target[1],y=target[2],label='LIGHT'}
end
local function marker(target,label,color,notice)
    local point=sc.camera.to_screen(target.x,target.y-16)
    local x,y=math.max(12,math.min(372,point.x)),math.max(notice and 110 or 76,math.min(162,point.y))
    local arrow=point.x<12 and '<' or point.x>372 and '>' or 'v'
    sc.text(arrow,x-3,y-12,12,color,true)
    local width=sc.measure(label,10)
    local left=math.max(4,math.min(380-width-8,x-width/2-4))
    sc.rect(left,y-30,width+8,16,'#101C2EE8',true)
    sc.text(label,left+4,y-27,10,color,true)
end
function Guide.draw(room,stage,target,notice,near,legend,use)
    legend=legend or 'A/D MOVE  SPACE JUMP  E USE  ESC MENU  R RESCUE'
    if sc.measure(legend,7)>368 then legend='CUSTOM CONTROLS / SEE SETTINGS > CONTROLS' end
    sc.rect(0,0,384,44,'#101C2EF5',true)
    sc.text(room..'/3  '..levels[room].name,8,5,11,'#E6EDF7',true)
    for i=1,5 do sc.rect(8+(i-1)*12,22,8,3,stage.lights['light.'..i] and '#70BFFF' or '#385777',true) end
    sc.text(legend,8,34,7,'#8CA4C4',true)
    sc.text(stage.open and 'POWER ON' or 'POWER OFF',282,7,10,stage.open and '#66D9B0' or '#FFCB77',true)
    if room==3 then
        for i=1,3 do sc.rect(332+(i-1)*12,23,8,3,stage.sequence>=i and '#66D9B0' or '#385777',true) end
    end
    if not near or math.abs(target.x-near.x)>1 then marker(target,target.label,'#70BFFF',notice) end
    if near then marker(near,'['..(use or 'E')..'] '..near.label,'#FFCB77',notice) end
    if notice then
        sc.rect(8,48,368,30,'#1D2C43F5',true)
        sc.text(notice,14,53,10,'#FFCB77',true,{wrap=356})
    end
    sc.rect(0,194,384,22,'#101C2EF5',true)
    sc.text(target.text,8,200,10,'#E6EDF7',true,{wrap=368})
end
return Guide
