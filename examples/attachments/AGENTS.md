Read README.md, rig.lua and docs/api.lua. Persist object IDs and local poses, never
runtime handles. Geometry and bodies belong to the authored definitions; restoring
uses one spawn_many transaction. Draw is read-only. Use the 40-frame smoke with an
isolated save directory; visual checks must be hidden, muted and bounded.
