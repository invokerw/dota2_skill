#pragma once

#include "dota/ability/ability.hpp"
#include "dota/modifier/enums.hpp"
#include "dota/modifier/modifier.hpp"

#include <string>
#include <variant>
#include <vector>

namespace dota {

// --- DataDriven action list ------------------------------------------------
//
// Each entry in `on_spell_start:` is parsed into one of these structs. Fields
// of type `std::string` that begin with '%' are templated references into
// ability_special and resolved at action-execution time using the ability's
// current level. Non-'%' numeric strings are parsed as plain numbers.

enum class ActionTargetSpec : std::uint8_t {
    Caster,
    Target,
};

struct ActionDamage {
    ActionTargetSpec target;
    DamageType       type;
    std::string      amount;   // raw expression, resolved per level
};

struct ActionHeal {
    ActionTargetSpec target;
    std::string      amount;
};

struct ActionApplyModifier {
    ActionTargetSpec target;
    std::string      modifier_name;
    std::string      duration;     // "" → permanent
};

using SpellAction = std::variant<ActionDamage, ActionHeal, ActionApplyModifier>;

// Parsed YAML ability definition — the recipe for constructing a runtime
// DataDrivenAbility. Lives in the AbilityRegistry as an immutable record.
struct AbilityDef {
    std::string    name;
    std::string    base_class;       // "ability_datadriven" or "ability_lua"
    std::uint32_t  behavior = 0;
    TargetTeam     target_team = TargetTeam::None;

    double              cast_point = 0.0;
    double              backswing = 0.0;
    double              channel_time = 0.0;
    double              cast_range = 0.0;
    std::vector<double> cooldowns;
    std::vector<double> mana_costs;
    AbilitySpecial      ability_special;

    // Present for base_class=="ability_datadriven".
    std::vector<SpellAction> on_spell_start;

    // Present for base_class=="ability_lua" (Stage 4).
    std::string script_path;
};

// Runtime DataDriven ability. Populates its timings/specials from an
// AbilityDef at construction, then replays the recorded action list at
// on_spell_start().
class DataDrivenAbility : public Ability {
public:
    DataDrivenAbility(Unit& caster, const AbilityDef& def);

    void on_spell_start(CastContext& ctx) override;

private:
    std::vector<SpellAction> actions_;
};

// Resolve a `%var` expression (or a plain numeric literal) using the
// ability's special table at `level`. Exposed for tests.
double resolve_expression(const std::string& expr,
                          const AbilitySpecial& special,
                          int level);

} // namespace dota
