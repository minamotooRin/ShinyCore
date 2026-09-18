# Third-party software

ShinyCore sources and original LANTERN assets are MIT licensed. Dependencies are downloaded only during CMake configuration; source archives are pinned and integrity checked.

| Dependency | Version | Source | Notice |
| --- | --- | --- | --- |
| Lua | 5.4.9 | https://www.lua.org/ftp/lua-5.4.9.tar.gz | [MIT](licenses/lua.txt) |
| Box2D | 3.1.1 | https://github.com/erincatto/box2d/tree/v3.1.1 | [MIT](licenses/box2d.txt) |
| yyjson | 0.12.0 | https://github.com/ibireme/yyjson/tree/0.12.0 | [MIT](licenses/yyjson.txt) |
| stb_truetype | header pinned from raylib 5.5 | https://github.com/raysan5/raylib/blob/5.5/src/external/stb_truetype.h | [MIT option](licenses/stb_truetype.txt) |
| Noto Sans SC subset (Workshop and text-input fixture) | weight 400, declared repertoire | https://github.com/google/fonts/tree/main/ofl/notosanssc | [SIL OFL](licenses/notosanssc.txt) |
| ENet (optional, `SHINY_NETWORK=ON`) | 1.3.18 | https://github.com/lsalzman/enet/tree/v1.3.18 | [MIT](licenses/enet.txt) |
| raylib | 5.5 | https://github.com/raysan5/raylib/releases/tag/5.5 | [zlib/libpng](licenses/raylib.txt) |
| GLFW | bundled with raylib 5.5 | https://github.com/raysan5/raylib/tree/5.5/src/external/glfw | [zlib/libpng](licenses/glfw.txt) |
| raylib bundled loaders, fonts and audio | archive-pinned | https://github.com/raysan5/raylib/tree/5.5/src/external | [Collected original notices](licenses/raylib-external.txt) |

The ASCII default-font width table in `src/content/text.cpp` is adapted from
raylib 5.5 `rtext.c`, copyright Ramon Santamaria. It shares the raylib license
above and provides matching layout metrics in graphics and headless builds.

The collected notices retain the original source comments for miniaudio, dr audio decoders, stb libraries, QOI/QOA, jar audio loaders, glad, compression utilities and platform helpers. Some are optional features in the pinned dependency rather than public ShinyCore APIs. Original license alternatives are preserved. ShinyCore does not include or license any Animal Well code, artwork or music.

For ENet 1.3.18, `cmake/enet-offset.cmake` generates a copy of `protocol.c`
with its three null-pointer header-offset expressions replaced by standard
`offsetof`. This avoids undefined behavior detected by UBSan without changing
the wire format. The downloaded archive and source tree remain unchanged.

Archive SHA-256 values:

```text
lua-5.4.9.tar.gz  2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
raylib-5.5.tar.gz aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e
enet-v1.3.18.tar.gz 28603c895f9ed24a846478180ee72c7376b39b4bb1287b73877e5eae7d96b0dd
box2d-v3.1.1.tar.gz fb6ef914b50f4312d7d921a600eabc12318bb3c55a0b8c0b90608fa4488ef2e4
yyjson-0.12.0.tar.gz b16246f617b2a136c78d73e5e2647c6f1de1313e46678062985bdcf1f40bb75d
stb_truetype.h ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab
```

Redistributable application packages produced by `tools/package.py` include this file, the project's LICENSE, and the notice files above. macOS framework libraries are provided by the operating system and are not copied into the package.

## utf8proc 2.10.0

UTF-8 grapheme boundaries use utf8proc (MIT, with Unicode data license).
Source: https://github.com/JuliaStrings/utf8proc/tree/v2.10.0
Full notices: licenses/utf8proc.txt.
