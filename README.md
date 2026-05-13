# dota2_skill

A Dota 2–style ability & modifier system, implemented in C++20 with Lua
scripting (sol2) and YAML data (yaml-cpp).

See [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) for the staged plan.

## Build

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/duel
```

Requires a C++20 compiler (AppleClang 15+, Clang 15+, GCC 12+, MSVC 19.30+).
Dependencies are fetched on first configure via [CPM.cmake](cmake/CPM.cmake).
