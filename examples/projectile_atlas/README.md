# Projectile atlas

Run `shiny examples/projectile_atlas --frames 8 --replay examples/projectile_atlas/smoke.txt --capture atlas.png` from the repository.
Eight regions reuse Lantern's original `keeper.png`; there are no ordinary entities
for the projectiles. The lower row mixes colored quads and tinted translucent sprites.
An early projectile expires to exercise dense removal without changing overlap order.
The simulation is stationary; smoke.txt isolates playback from real device input.
Use the same replay and eight frames with `--headless` for comparison.

This is a rendering fixture, not the 20,000-projectile performance acceptance scene.
