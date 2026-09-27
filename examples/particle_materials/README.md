# Particle textures and blending

Run `shiny examples/particle_materials --frames 8 --replay examples/particle_materials/smoke.txt --capture materials.png`.
The top row compares alpha, additive and mixed order. The bottom row reuses Lantern's
original keeper atlas and checks that plain geometry restores the white texture.
An early particle expires before capture to exercise stable compaction.
No Python or image library is needed by the game. The optional pixel verification
script `tests/particle_materials.py` needs Pillow on the development machine.
