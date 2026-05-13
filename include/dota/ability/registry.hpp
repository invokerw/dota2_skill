#pragma once

#include "dota/ability/datadriven.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace dota {

class Ability;
class LuaState;
class Unit;

// Stores parsed AbilityDefs indexed by name. Loading a YAML file registers
// every ability inside it. Construct a runtime Ability with
// `instantiate(name, caster)`.
class AbilityRegistry {
public:
    // Load a YAML file containing an `abilities:` list and/or a `hero:` block.
    // Returns the number of abilities registered. Throws std::runtime_error
    // on malformed input.
    std::size_t load_file(const std::string& path);

    // Register a parsed definition directly (bypass YAML — useful for tests).
    void register_def(AbilityDef def);

    const AbilityDef* find(const std::string& name) const;

    // Construct a runtime Ability (DataDriven or Scripted depending on
    // def.base_class) and attach it to the caster. Returns a non-owning
    // pointer on success, nullptr if the ability is not registered or the
    // required Lua backing is missing.
    //
    // For Lua-based abilities the caller must supply the LuaState. If the
    // registry has a default LuaState set via `set_lua()`, that one is used.
    Ability* instantiate(const std::string& name, Unit& caster,
                         LuaState* lua = nullptr);

    void      set_lua(LuaState* lua) { default_lua_ = lua; }
    LuaState* lua() const            { return default_lua_; }

private:
    std::unordered_map<std::string, AbilityDef> defs_;
    LuaState* default_lua_ = nullptr;
};

} // namespace dota
