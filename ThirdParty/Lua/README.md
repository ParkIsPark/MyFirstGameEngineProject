# Lua 5.4.9

This directory vendors the official Lua 5.4.9 source archive for the engine's
embedded scripting runtime.

- Origin: https://www.lua.org/ftp/lua-5.4.9.tar.gz
- SHA-256: `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`
- License: the upstream MIT license is preserved in
  [`5.4.9/doc/readme.html`](5.4.9/doc/readme.html).
- Local modifications: none. The archive was extracted unchanged; the Visual
  Studio project manifests select the embeddable library sources and exclude
  the standalone `lua.c` and `luac.c` programs.
