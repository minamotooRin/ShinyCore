# Native implementation

`core/` and `physics/` implement simulation; `content/` owns validated data and storage; `script/` is the Lua boundary. `runtime/` coordinates rooms and fixed frames. `platform/`, `render/`, `audio/` and `net/` adapt optional devices; `dev/` is opt-in tooling.

Public headers live in `../include/shiny/`. Keep platform, GPU and Lua dependencies out of simulation code.
