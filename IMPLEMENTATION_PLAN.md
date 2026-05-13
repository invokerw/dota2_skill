# Dota 2 Skill System — Implementation Plan

A C++20 core + Lua (sol2) scripting + YAML (yaml-cpp) data implementation of a
Dota-style ability & modifier system. No modules, no coroutines.

## Tech Stack

| Layer | Choice | Dota analog |
|---|---|---|
| Engine core | C++20 (CMake) | Source 2 C++ |
| Script layer | Lua 5.4 + sol2 | VScripts |
| Data layer | YAML (yaml-cpp) | KeyValues |
| Tests | GoogleTest | — |
| Deps | CPM.cmake | — |

Compiler targets: AppleClang 15+, Clang 15+, GCC 12+, MSVC 19.30+.

---

## Stage 1: Scaffolding + Core Entities + Event Bus
**Goal**: Two `Unit`s in a fixed-tick `World` auto-attack each other to death.

**Deliverables**:
- `CMakeLists.txt` with CPM fetching GoogleTest (yaml-cpp/sol2/lua pulled in later stages)
- `Unit`: hp / mp / armor / magic_resist / attack_damage / attack_speed / move_speed / team / position
- `EventBus`: templated `subscribe<E>(fn)` / `publish(E&)`, supports mutable events
- `World`: 30Hz fixed tick, `advance(dt)`, `find_enemies_in_radius`
- Minimal auto-attack loop driven by events

**Tests**:
- `test_event_bus`: subscribe/publish counts, mutable event modification
- `test_unit_basic`: HP clamp, death event fires once, auto-attack DPS reasonable

**Success Criteria**: `ctest` all green; `examples/duel` prints a legible fight log.

**Status**: Complete (11/11 tests passing, duel demo runs end-to-end)

---

## Stage 2: Modifier System
**Goal**: Full Property/State/Event three-part modifier model.

**Deliverables**:
- `ModifierProperty` enum (armor/attack-speed/hp/incoming-damage-pct…)
- `ModifierState` enum (stunned/silenced/rooted/invulnerable/invisible)
- `Modifier` base with `declare_properties / declare_states / on_event`
- `ModifierManager`: attach/remove, duration, think_interval, stack_count
- Property aggregator: BONUS_CONSTANT → BONUS_PCT → TOTAL_PCT → OVERRIDE
- Generic modifiers: stunned / silenced / rooted / invisible / generic_stats

**Tests**:
- `test_modifier_property`: three-layer armor aggregation correctness
- `test_modifier_state`: stun gates `can_cast / can_attack`, auto-expires
- `test_modifier_event`: `on_take_damage` can mutate damage (shield / reflect)

**Status**: Complete (33/33 tests passing; Unit stat getters route through aggregator; basic attacks flow through `apply_damage` pipeline)

---

## Stage 3: Ability Framework + DataDriven (YAML) Loader
**Goal**: Declarative abilities run the full Dota cast lifecycle.

**Deliverables**:
- `Ability` base: cast_point / backswing / cooldown / mana_cost / level / ability_special
- `BehaviorFlag` bitmask
- Cast state machine: Ready → Casting → Cast → Backswing → Cooldown
- Legality checks: silence/cooldown/mana/target-valid/magic-immune
- YAML loader: `AbilityDef` built via yaml-cpp; `on_spell_start` parsed into `Action`s (ApplyModifier / Damage / Heal / RunScript)
- `%var` templating from `ability_special` indexed by `level`

**Tests**:
- `test_datadriven_loader`: field mapping + per-level value indexing
- `test_ability_lifecycle`: stun during cast_point aborts `on_spell_start`; cooldown gates recast
- `test_lion_earth_spike`: end-to-end damage + stun application

**Status**: Complete (47/47 tests passing; yaml-cpp 0.8.0 wired; Lion Earth Spike runs end-to-end from YAML)

---

## Stage 4: Lua Integration
**Goal**: Complex abilities/modifiers authorable in Lua, mirroring VScripts.

**Deliverables**:
- `LuaState` + sol2 bindings for `Unit / Ability / Modifier / World / Damage / DamageType / EventBus`
- `ScriptedAbility`: `base_class: ability_lua` loads a Lua table, forwards `on_spell_start / on_channel_think / on_channel_finish / on_upgrade`
- `ScriptedModifier`: Lua-defined modifier with declared properties + event hooks
- Error handling: Lua errors log & degrade, never crash world tick

**Tests**:
- `test_lua_bindings`: enum tables, Unit/World methods reachable, `apply_damage` flows through full pipeline
- `test_lua_ability`: Blade Fury (channel + AoE) pulses damage, stun interrupts, cooldown starts on end
- `test_lua_modifier`: Lua `modifier_test_shield` declares MagicResistBonus property, absorbs only magical damage
- `test_lua_error_safety`: runtime errors in Lua hooks are trapped via `sol::protected_function`; error callback counts upward

**Status**: Complete (64/64 tests passing; Lua 5.4 + sol2 via CPM; ScriptedAbility + ScriptedModifier both exercised by tests; Juggernaut Blade Fury runs from YAML+Lua)

---

## Stage 5: Damage / Heal Pipeline
**Goal**: Pluggable pipeline; modifiers intervene at any stage.

**Deliverables**:
- `DamageContext`: attacker/victim/amount/type/flags/source_ability
- Pipeline: PreAmp → TypeResistance → Shield/Absorb → FinalAmp → Apply → PostEvent
- Dota formulas: armor `0.06*a / (1 + 0.06*|a|)`, magic resist multiplicative
- Flags: HP_LOSS / REFLECTION / BYPASS_MAGIC_IMMUNITY / NO_SPELL_AMPLIFICATION
- Independent `HealPipeline`

**Tests**:
- `test_damage_pipeline`: physical armor curve, magic resist, magic immune + BypassMagicImmune flag, pure ignores resist, outgoing/incoming amp stacking, NoSpellAmplification flag, HPLoss skips shields+resist, Blade-Mail reflect with Reflection flag prevents loops
- `test_heal_pipeline`: base heal, break-the-healing reduces heal, HealAmpPct stacks both directions, dead unit immune, clamp to max HP

**Status**: Complete (78/78 tests passing; `combat/damage.hpp` pipeline powers Unit::apply_damage + World basic attack; Blade-Mail reflect + break-the-healing land in the modifier library)

---

## Stage 6: Three Heroes + CLI Duel Demo
**Goal**: Cover active / passive / channel / summon / multi-hit patterns.

**Heroes**:
- **Lion** (all YAML): Earth Spike / Hex / Mana Drain (channel) / Finger of Death
- **Lina** (YAML + Lua passive): Dragon Slave / Light Strike Array (delay AOE) / Fiery Soul (Lua modifier, stacking) / Laguna Blade
- **Juggernaut** (Lua-heavy): Blade Fury (channel AOE) / Healing Ward (summon aura) / Blade Dance (Lua passive crit) / Omnislash

**Deliverables**:
- Full YAML + Lua content for the three heroes
- `examples/duel` 1v1 CLI demo with a legible combat log

**Tests**: Per-ability integration tests (cooldown / mana / damage / duration).

**Status**: Not Started
