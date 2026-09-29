# ShinyCore documentation

Start with [architecture](architecture.md), the [API reference](api-reference.md), and the [Agent guide](llm-guide.md). For the executable's exact capabilities and defaults, run `shiny --api`; [LuaLS annotations](api.lua) support editing.

| Area | Read this first | Covers |
| --- | --- | --- |
| Runtime | [Runtime index](runtime/README.md) | Lifecycle, state, saves, input actions, debugging |
| World | [World index](world/README.md) | Objects, physics, navigation, projectiles |
| Content | [Content index](content/README.md) | Tiled import, streaming, image residency |
| Presentation | [Presentation index](presentation/README.md) | UI, animation, rendering, audio |
| Network | [Network index](network/README.md) | Sessions, snapshots, rejoining |
| Workflows | [Guides](guides/README.md) | Building assets, packaging, profiling, verification |

[Input](input.md) and [networking](networking.md) remain at stable paths used by project scaffolding and packaging. [Verification](verification/README.md) contains historical test notes; the [closeout audit](acceptance-audit-20260928.md) states the current evidence and remaining limits. Historical reports do not certify a new build.
