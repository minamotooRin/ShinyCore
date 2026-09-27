local Rig=require('rig')
local ids,message
local function object(name) return assert(sc.identity.resolve('rig/'..name).id) end
return {width=384,height=216,gravity=0,ambient=1,map={rows={'.'},background='#111C2EFF'},
    init=function()
        local saved=sc.state.get('rig')
        ids=Rig.spawn(saved or Rig.initial())
        message=saved and 'RESTORED / new runtime handles' or 'READY / three persistent objects'
        sc.app.pause(true);sc.presentation.interpolate(true);sc.camera.set{x=0,y=0,bounds=false}
    end,
    update=function()
        if sc.input.key_pressed('space') then sc.app.pause(not sc.app.paused()) end
        if sc.input.key_pressed('d') then sc.presentation.detach(object('lamp'));message='DETACHED / world pose retained' end
        if sc.input.key_pressed('a') then
            sc.presentation.attach(object('lamp'),object('body'),{x=48,y=0,angle=.4});message='ATTACHED / native following'
        end
        if sc.input.key_pressed('s') then
            sc.state.set('rig',Rig.capture(ids))
            local ok,err=sc.save.write('rig');message=ok and 'SAVED / persistent IDs only' or err
        end
        if sc.input.key_pressed('l') then
            local ok,err=sc.save.load('rig');if not ok then message=err end
        end
        if sc.input.key_pressed('r') then sc.state.set('rig',nil);sc.scene('main.lua') end
        sc.debug.watch('rig',Rig.capture(ids))
        sc.debug.watch('paused',sc.app.paused())
    end,
    draw=function()
        sc.text('PERSISTENT ATTACHMENTS',16,18,18,'#E6EDF7',true)
        sc.text(message,16,48,10,'#A4B8D4',true)
        sc.text(sc.app.paused() and 'PAUSED' or 'ROTATING',16,148,12,'#E6EDF7',true)
        sc.text('SPACE play/pause  D detach  A attach',16,172,10,'#A4B8D4',true)
        sc.text('S save  L load  R reset',16,194,10,'#A4B8D4',true)
    end}
