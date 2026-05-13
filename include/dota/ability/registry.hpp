#pragma once

#include "dota/ability/datadriven.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace dota {

class Unit;

// Stores parsed AbilityDefs indexed by name. Loading a YAML file registers
// every ability inside it. Construct a runtime DataDrivenAbility with
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

    // Construct a runtime DataDrivenAbility and hand it off to the caster's
    // AbilityManager. Returns a non-owning pointer on success, nullptr if
    // there is no definition for `name` or it is Lua-based (Stage 4).
    DataDrivenAbility* instantiate(const std::string& name, Unit& caster);

private:
    std::unordered_map<std::string, AbilityDef> defs_;
};

} // namespace dota
