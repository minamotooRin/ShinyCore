-- Bindings are ordinary Lua data; query names are case-sensitive.
local keys = {"apostrophe","comma","minus","period","slash","0","1","2","3","4","5","6","7","8","9","semicolon","equal","a","b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q","r","s","t","u","v","w","x","y","z","left_bracket","backslash","right_bracket","grave","space","escape","enter","tab","backspace","insert","delete","right","left","down","up","page_up","page_down","home","end","caps_lock","scroll_lock","num_lock","print_screen","pause","f1","f2","f3","f4","f5","f6","f7","f8","f9","f10","f11","f12","left_shift","left_control","left_alt","left_super","right_shift","right_control","right_alt","right_super","kb_menu","kp_0","kp_1","kp_2","kp_3","kp_4","kp_5","kp_6","kp_7","kp_8","kp_9","kp_decimal","kp_divide","kp_multiply","kp_subtract","kp_add","kp_enter","kp_equal"}
local buttons = {"dpad_up","dpad_right","dpad_down","dpad_left","north","east","south","west","left_shoulder","left_trigger","right_shoulder","right_trigger","back","guide","start","left_thumb","right_thumb"}
local axes = {"left_x","left_y","right_x","right_y","left_trigger","right_trigger"}
local function active(names, query)
    local result = {}
    for _, name in ipairs(names) do if query(name) then result[#result+1]=name end end
    local text = #result == 0 and "-" or table.concat(result, " ")
    return #text > 72 and text:sub(1,69).."..." or text
end
return {
    title="ShinyCore Input Lab", width=640, height=360, gravity=0,
    draw=function()
        sc.rect(0,0,640,360,"#101B2D",true)
        sc.text("INPUT LAB",20,16,24,"#7EE8D0",true)
        sc.text("Keyboard + single gamepad | fixed-tick snapshots",20,48,12,"#A8BDDA",true)
        sc.text("Held: "..active(keys,sc.key_down),20,78,12,"#FFFFFF",true)
        sc.text("Pressed: "..active(keys,sc.key_pressed),20,100,12,"#7EE8D0",true)
        sc.text("Released: "..active(keys,sc.key_released),20,122,12,"#F4C88A",true)
        sc.text("Gamepad: "..(sc.gamepad_connected() and "CONNECTED" or "DISCONNECTED"),20,155,16,"#FFFFFF",true)
        sc.text("Held: "..active(buttons,sc.gamepad_down),20,183,12,"#A8BDDA",true)
        sc.text("Pressed: "..active(buttons,sc.gamepad_pressed),20,203,12,"#7EE8D0",true)
        sc.text("Released: "..active(buttons,sc.gamepad_released),20,223,12,"#F4C88A",true)
        for i,axis in ipairs(axes) do
            local col=(i-1)%3; local row=math.floor((i-1)/3)
            sc.text(string.format("%s: %.2f",axis,sc.gamepad_axis(axis)),20+col*205,255+row*25,12,"#FFFFFF",true)
        end
        sc.text("Esc / P / O / F keys belong to the game. Close window to exit.",20,328,10,"#A8BDDA",true)
    end,
}
