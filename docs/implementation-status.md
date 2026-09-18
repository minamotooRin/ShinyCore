# Complete edition implementation ledger

This ledger records implementation evidence, not release claims. The approved
complete-edition specification is the acceptance contract. All work belongs to
one delivery. Unchecked requirements remain outstanding.

Latest local evidence: [2026-09-18 development verification](verification-complete-dev.md).
Implemented subsets include application settings with rollback, native input and
IME plumbing, retained Lua UI/text editing, bounded projectile/navigation systems,
resource imports, chunk-file cache, save backups, shared audio and named network
sessions. Each large work package below still has unfinished requirements.

## Work packages

- [ ] Runtime ownership, configurable pools, stale handles, atomic batch edits
- [ ] Input snapshots, four controllers, recording, Windows IME
- [ ] Declarative UI, text editing, settings, focus, virtual lists
- [ ] Projectile batches, navigation, physics queries and joints
- [ ] Resource pipeline, expanded Tiled import, streaming and persistence
- [ ] Animation, particles, cameras, materials, lighting and postprocessing
- [ ] Audio buses, shared resources and persistent music
- [ ] Save slots, backup recovery, application-owned network sessions
- [ ] Agent API contract, tracing, profiling, debugger, portable SDK
- [ ] Three playable samples and multiplayer fixture
- [ ] Five benchmark loads and original performance thresholds
- [ ] Sanitizers, native Windows evidence, desktop CI and portable packages
- [ ] Two-hour endurance and physical controller/IME/audio acceptance

## Constraints

No skeleton animation, old-version compatibility layer or migration guide.
No cloud services, general ECS, editor database or provider integration.
Native, performance and platform checks must report actual measured evidence.
