#pragma once

#include "dota/ability/ability.hpp"
#include "dota/modifier/enums.hpp"
#include "dota/modifier/modifier.hpp"

#include <string>
#include <variant>
#include <vector>

namespace dota {

// --- DataDriven 动作列表 ------------------------------------------------
//
// `on_spell_start:` 中的每个条目都会被解析为这些结构体之一。类型为
// `std::string` 且以 '%' 开头的字段是对 ability_special 的模板引用，
// 在动作执行时使用技能的当前等级解析。非 '%' 的数字字符串会被解析为普通数字。

enum class ActionTargetSpec : std::uint8_t {
    Caster,
    Target,
};

struct ActionDamage {
    ActionTargetSpec target;
    DamageType       type;
    std::string      amount;   // 原始表达式，按等级解析
};

struct ActionHeal {
    ActionTargetSpec target;
    std::string      amount;
};

struct ActionApplyModifier {
    ActionTargetSpec target;
    std::string      modifier_name;
    std::string      duration;     // "" → 永久
};

using SpellAction = std::variant<ActionDamage, ActionHeal, ActionApplyModifier>;

// 解析后的 YAML 技能定义 — 构造运行时
// DataDrivenAbility 的配方。作为不可变记录存在于 AbilityRegistry 中。
struct AbilityDef {
    std::string    name;
    std::string    base_class;       // "ability_datadriven" 或 "ability_lua"
    std::uint32_t  behavior = 0;
    TargetTeam     target_team = TargetTeam::None;

    double              cast_point = 0.0;
    double              backswing = 0.0;
    double              channel_time = 0.0;
    double              cast_range = 0.0;
    std::vector<double> cooldowns;
    std::vector<double> mana_costs;
    AbilitySpecial      ability_special;

    // 当 base_class=="ability_datadriven" 时存在。
    std::vector<SpellAction> on_spell_start;

    // 当 base_class=="ability_lua" 时存在（阶段 4）。
    std::string script_path;
};

// 运行时 DataDriven 技能。在构造时从 AbilityDef 填充其
// 时间/特殊值，然后在 on_spell_start() 时重放记录的动作列表。
class DataDrivenAbility : public Ability {
public:
    DataDrivenAbility(Unit& caster, const AbilityDef& def);

    void on_spell_start(CastContext& ctx) override;

private:
    std::vector<SpellAction> actions_;
};

// 使用技能在 `level` 等级的特殊值表解析 `%var` 表达式
//（或普通数字字面量）。为测试而暴露。
double resolve_expression(const std::string& expr,
                          const AbilitySpecial& special,
                          int level);

} // namespace dota
