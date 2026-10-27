# DLW vendored Lua provenance

- Upstream: PUC-Lua
- Release: Lua 5.4.8
- Archive: `https://www.lua.org/ftp/lua-5.4.8.tar.gz`
- Upstream archive SHA-256:
  `4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`
- License: MIT, reproduced verbatim in `LICENSE` and staged beside
  `dlw_lua54.dll` as `LICENSE_LUA_5.4.8.txt`

DLW builds the Lua core plus restricted base, table, string, math, UTF-8, and
coroutine libraries into a private helper DLL. It intentionally excludes the
normal I/O, OS, package/dynamic loader, and debug libraries.
