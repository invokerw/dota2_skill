# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure    # all 91 tests
ctest --test-dir build -R "HeroLinaTest"      # run one test suite
./build/duel                                   # 2v1 team-fight demo
```

Requires C++20 (AppleClang 15+, Clang 15+, GCC 12+). All dependencies (GoogleTest, yaml-cpp, Lua 5.4, sol2) auto-fetch via CPM.cmake on first configure.

After any source change, rebuild with `cmake --build build -j` — there's no separate lint step.

## Architecture

This is a Dota 2–style ability and modifier system: C++20 engine core, Lua 5.4 scripting (via sol2), YAML data definitions (via yaml-cpp).

### Layers

| Layer | Location | Role |
|-------|----------|------|
| Core engine | `include/dota/core/`, `src/core/` | Unit, World (30Hz tick), EventBus |
| Modifier system | `include/dota/modifier/`, `src/modifier/` | Property aggregation, state bitmask, lifecycle hooks |
| Ability framework | `include/dota/ability/`, `src/ability/` | Cast state machine, DataDriven (YAML) + Scripted (Lua) |
| Combat pipeline | `include/dota/combat/`, `src/combat/` | Staged damage/heal pipeline with modifier intervention |
| Lua bindings | `src/script/` | sol2 usertypes for Unit/World/Vec2, enum tables |
| Data | `data/heroes/*.yaml` | Hero definitions with ability_special per-level values |
| Scripts | `scripts/abilities/*.lua` | Lua ability implementations |

### Key design patterns

- **Two ability flavors**: `ability_datadriven` (pure YAML action list) vs `ability_lua` (Lua table with `on_spell_start`/`on_channel_think`/`on_channel_finish`). The `base_class` field in YAML controls which.
- **Modifier three-part model**: `declared_properties()` (numeric bonuses aggregated by layer), `declared_states()` (bitmask), event hooks (`on_pre_take_damage`, `on_interval_think`, etc.). Property values multiply by `stack_count`.
- **Damage pipeline** (`deal_damage` in `src/combat/damage.cpp`): OutgoingAmp → IncomingAmp → PreTake (shields) → MagicImmune check → TypeResistance → Apply → PostTake (reflect). DamageFlag bitmask gates each stage.
- **Heal pipeline** (`deal_heal`): PreTakeHeal → HealAmpPct → clamp → PostTakeHeal.
- **Cast lifecycle**: Ready → Casting (cast_point) → fires `on_spell_start` → Backswing → OnCooldown. Channelled abilities branch into Channelling with per-tick `on_channel_think`.
- **ScriptedAbility self-table**: Lua hooks receive a `self` table with closures (`get_special`, `target_point`, `target_unit`, `level`, `get_caster`). The leading `sol::object` parameter handles colon-call syntax.

### Compile-time defines

- `DOTA_SCRIPT_DIR` — absolute path to `scripts/` (set on `dota_core`)
- `DOTA_DATA_DIR` — absolute path to `data/` (set on test and duel targets)

### Adding a new hero

1. Create `data/heroes/<name>.yaml` with hero stats + abilities list
2. For `ability_lua` entries, create `scripts/abilities/<ability_name>.lua` returning a table with lifecycle hooks
3. Add integration tests in `tests/test_hero_<name>.cpp` and register the file in `CMakeLists.txt`

### Adding a new modifier property

1. Add enum entry in `include/dota/modifier/enums.hpp` (`ModifierProperty`)
2. Map its layer in `layer_of()` (same file)
3. Route it through the relevant Unit stat getter in `src/core/unit.cpp`
4. Expose to Lua in `src/script/bindings.cpp` → `property_table()`
