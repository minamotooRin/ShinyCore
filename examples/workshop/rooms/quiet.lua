return {
    title='ShinyCore / Quiet Room',width=384,height=224,gravity=0,ambient=1,
    init=function() sc.state.set('visited_quiet',true) end,
    update=function() if sc.pressed('action') then sc.scene('main.lua') end end,
    draw=function()
        sc.rect(0,0,384,224,'#142C35',true)
        sc.text('已收集 '..tostring(sc.state.get('coins') or 0),32,70,24,'#F4D99C',true,{font='ui'})
        sc.text('返回  E',32,112,16,'#A1C5B8',true,{font='ui'})
    end,
}
