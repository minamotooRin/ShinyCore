-- Example: project.data_version=2, migrate="game.migrate" accepts version 1 data.
-- The engine passes an independent data copy and forbids engine mutation here.
return function(old_version, new_version, data)
    assert(old_version == 1 and new_version == 2, "unsupported checkpoint version")
    if data.gold ~= nil then data.coins, data.gold = data.gold, nil end
    return data
end
