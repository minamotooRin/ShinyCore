return {
    id = "shiny.workshop", data_version = 2, migrate = "game.migrate",
    entry = "main.lua", rooms = {"main.lua", "rooms/quiet.lua"},
    resources = {
        keeper = {type="image", path="assets/keeper.png"},
        tiles = {type="image", path="assets/tiles.png"},
        chime = {type="sound", path="assets/chime.wav"},
        theme = {type="music", path="assets/theme.ogg"},
        ui = {type="font", path="assets/workshop.ttf", size=16,
            characters="星灯工坊移动跳跃下落保存读取进度房间返回已收集开始继续中文像素世界箱子平台斜坡欢迎"},
    },
}
