# Working on Constellation

Keep the game self-contained. `game/protocol.lua` owns wire validation,
`game/server.lua` owns authority, `game/client.lua` owns interpolation and retries,
and `game/scene.lua` owns presentation. Do not move game rules into native bindings.

Only update may mutate sessions. Candidate room init binds and reads existing state;
it must not send packets or change application services. Never put rejoin tokens
in watches, traces, logs, game saves or committed fixtures.

Run `shiny --check-all .` from this directory. From the engine repository, run
`tests/constellation_integration.py` and `tests/constellation_faults.py` with a
network-enabled executable; see README for commands. Validate visual changes
with `--native` and inspect the actual captured image. Do not weaken load or
protocol assertions to hide a networking failure.
