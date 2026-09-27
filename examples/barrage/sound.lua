-- Presentation only. Clocks are room-local, bounded and never use the gameplay RNG.
local Sound={}
local cues={
    shot={volume=.10,priority=-4,gap=.10},
    hit={volume=.14,priority=-2,gap=.09},
    ['break']={volume=.18,priority=0,gap=.12},
    dash={volume=.22,priority=2,gap=.2},
    hurt={volume=.32,priority=8,gap=.3},
    heal={volume=.22,priority=4,gap=.25},
    wave={volume=.24,priority=6,gap=.5,bus='ui'},
    win={volume=.30,priority=10,gap=1,bus='ui'},
    lose={volume=.28,priority=10,gap=1,bus='ui'},
    upgrade={resource='chime',volume=.25,priority=6,gap=.2,bus='ui'},
}
function Sound.new() return {time=0,ready={}} end
function Sound.update(sound,dt) sound.time=sound.time+dt end
function Sound.play(sound,name,x)
    local cue=assert(cues[name],'unknown combat sound')
    if sound.time<(sound.ready[name] or 0) then return end
    sound.ready[name]=sound.time+cue.gap
    -- Capacity rejection is cosmetic; high-priority cues may preempt low ones.
    return sc.audio.play(cue.resource or name,{volume=cue.volume,priority=cue.priority,
        bus=cue.bus or 'sfx',pan=x and math.max(-.8,math.min(.8,(x-192)/192)) or 0})
end
return Sound
