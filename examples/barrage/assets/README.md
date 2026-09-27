# Asset provenance

keeper.png, chime.wav and theme.ogg are unchanged copies of the original ShinyCore
Workshop assets; wisp.png is from Lantern. All are covered by this repository's
MIT license. The project uses local files and does not download runtime assets.

`arena-v1.png` is a new background generated on 2026-09-27 with the built-in
image generation tool, without reference images. The exact request is saved in
`arena-prompt.txt`: a low-contrast, orthographic night-time stone court with a
flush six-segment beacon seal and clear playable interior. The returned PNG is
1672×940, 1,549,752 bytes (about 6 MiB decoded RGBA); it is used unmodified and
scaled to the 384×216 scene with one background-layer command.
The floor and seal are decorative; they introduce no colliders or gameplay rules.
The generated source and prompt are bundled locally with the original assets.

`scout-v1.png`, `runner-v1.png`, `gunner-v1.png`, `brute-v1.png` and
`guardian-v1.png` are original four-frame pixel sprites authored for this game.
`tools/build_barrage_sprites.py` regenerates them with Pillow at their native
7–24 pixel sizes. Their silhouettes and color accents distinguish enemy behavior;
the original wisp atlas remains for keeper projectiles. The sprite builder never
reads challenge state or random numbers. These files are covered by the repository
MIT license.

`audio/*.wav` are original synthesized cues authored for this sample: shot, hit,
break, dash, hurt, heal, wave, win and lose. Rebuild from the repository root with
`python tools/build_barrage_audio.py examples/barrage/assets/audio`. The standalone
stdlib generator uses fixed chirps, local deterministic noise and note envelopes;
it never reads gameplay RNG. The nine mono 22,050 Hz / 16-bit PCM files total
145,708 bytes and are covered by the repository MIT license. Runtime needs only
the local WAV files, not Python. Original chime/theme assets remain unchanged.
