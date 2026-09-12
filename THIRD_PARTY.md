# Third-party software

ShinyCore sources and original LANTERN assets are MIT licensed. Dependencies are downloaded only during CMake configuration; source archives are pinned and integrity checked.

| Dependency | Version | Source | Notice |
| --- | --- | --- | --- |
| Lua | 5.4.9 | https://www.lua.org/ftp/lua-5.4.9.tar.gz | [MIT](licenses/lua.txt) |
| ENet (optional, `SHINY_NETWORK=ON`) | 1.3.18 | https://github.com/lsalzman/enet/tree/v1.3.18 | [MIT](licenses/enet.txt) |
| raylib | 5.5 | https://github.com/raysan5/raylib/releases/tag/5.5 | [zlib/libpng](licenses/raylib.txt) |
| GLFW | bundled with raylib 5.5 | https://github.com/raysan5/raylib/tree/5.5/src/external/glfw | [zlib/libpng](licenses/glfw.txt) |
| raylib bundled loaders, fonts and audio | archive-pinned | https://github.com/raysan5/raylib/tree/5.5/src/external | [Collected original notices](licenses/raylib-external.txt) |

The collected notices retain the original source comments for miniaudio, dr audio decoders, stb libraries, QOI/QOA, jar audio loaders, glad, compression utilities and platform helpers. Some are optional features in the pinned dependency rather than public ShinyCore APIs. Original license alternatives are preserved. ShinyCore does not include or license any Animal Well code, artwork or music.

Archive SHA-256 values:

```text
lua-5.4.9.tar.gz  2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
raylib-5.5.tar.gz aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e
enet-v1.3.18.tar.gz 28603c895f9ed24a846478180ee72c7376b39b4bb1287b73877e5eae7d96b0dd
```

Redistributable application packages produced by `tools/package.py` include this file, the project's LICENSE, and the notice files above. macOS framework libraries are provided by the operating system and are not copied into the package.
