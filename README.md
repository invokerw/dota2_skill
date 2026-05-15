# dota2_skill

A Dota 2–style ability & modifier system, implemented in C++20 with Lua
scripting (sol2) and YAML data (yaml-cpp).

## Build

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/duel
```

Requires a C++20 compiler (AppleClang 15+, Clang 15+, GCC 12+, MSVC 19.30+).
Dependencies are fetched on first configure via [CPM.cmake](cmake/CPM.cmake).

## Editor setup

The CMake project exports `build/compile_commands.json` for C++ language
servers and IDE code navigation. After running `cmake -B build`, VS Code's
Microsoft C/C++ extension will use the workspace settings in
[.vscode/settings.json](.vscode/settings.json).

If code navigation still looks stale, reset the IntelliSense database and reload
the VS Code window:

```sh
# VS Code command palette:
# C/C++: Reset IntelliSense Database
# Developer: Reload Window
```
