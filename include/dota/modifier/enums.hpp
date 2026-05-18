#pragma once

#include <cstdint>

namespace dota {

// 修饰器可以贡献的数值属性. 这些属性对应 Valve 的 MODIFIER_PROPERTY_* 命名空间,
// 但精简为 Stage 2 所需的部分. 添加新属性需要: (1) 新增枚举项, (2) 在 Unit 的
// 属性获取器中添加查询, (3) 可选地记录其参与的 aggregation(聚合)层级(常量 vs 百分比 vs 覆盖)
// -- 参见 PropertyLayer.
enum class ModifierProperty : std::uint16_t {
    // 护甲
    ArmorBonus = 0,                    // 常量
    ArmorBonusPct,                     // 百分比(护甲乘法)

    // 生命 / 魔法
    HealthBonus,                       // 最大生命常量
    ManaBonus,                         // 最大魔法常量

    // 攻击
    AttackDamageBonus,                 // 基础攻击力常量
    AttackDamageBonusPct,              // 总攻击力百分比
    AttackSpeedBonusConstant,          // 攻击速度常量(例如 +40 AS)

    // Resistance(抗性)
    MagicResistBonus,                  // 魔法 resistance(抗性)常量(0..1)

    // 伤害 amplification(增幅)(在伤害管线 Stage 5 中应用; 在此声明以便属性和测试
    // 可以提前推理它们).
    IncomingDamagePct,                 // 承受伤害 amplification(增幅)百分比
    OutgoingDamagePct,                 // 输出伤害 amplification(增幅)百分比

    // 移动
    MoveSpeedBonusConstant,            // 移动速度常量(固定加成, 例如 +30)
    MoveSpeedBonusPct,                 // 移动速度百分比

    // 治疗 amplification(增幅)(Stage 5 治疗管线). 值会被求和并以 (1 + sum) 的形式应用.
    // 值为 -0.4 表示破坏治疗(-40%).
    HealAmpPct,                        // 承受治疗 amplification(增幅)百分比

    // --- 扩展属性(Phase 0 补齐)---
    Evasion,                           // 闪避概率(0..1), 对 Pct 层求和后 clamp ≤ 0.95
    LifestealPct,                      // 物理吸血百分比(0..1)
    HealthRegen,                       // 每秒生命回复(常量)
    ManaRegen,                         // 每秒魔法回复(常量)
    SpellAmplifyPct,                   // 仅对法术伤害的输出 amplification(增幅)
    StatusResistancePct,               // 控制 resistance(抗性), 缩短 disable 持续时间
    CooldownReductionPct,              // 冷却缩减百分比
    CastRangeBonus,                    // 施法距离常量加成

    Count_                             // 哨兵值
};

// ModifierProperty 贡献的层级. Aggregation(聚合)顺序为:
//   final = (base + sum(CONSTANT)) * (1 + sum(PERCENTAGE)) * product(TOTAL_PCT)
// 如果任何修饰器声明了 OVERRIDE, 则 OVERRIDE 获胜(最后写入语义).
enum class PropertyLayer : std::uint8_t {
    Constant    = 0,
    Percentage  = 1,
    TotalPercentage = 2,
    Override    = 3,
};

// 布尔状态. 在 ModifierManager 内部以位掩码存储, 因此任何修饰器
// 声明某个状态都会导致该状态在单位上激活.
enum class ModifierState : std::uint8_t {
    Stunned = 0,
    Silenced,
    Rooted,
    Disarmed,
    Hexed,                 // 类似眩晕但允许使用物品; 稍后用于狮子的妖术
    Invisible,
    Invulnerable,
    OutOfGame,             // 例如全能斩施法者处于游戏外
    MagicImmune,

    // --- 扩展状态(Phase 0 补齐)---
    Untargetable,          // 不可被技能选中(仍可被 AoE 命中除非也 Invulnerable)
    NoUnitCollision,       // 不与其它单位发生碰撞, 用于 Hook 拉拽过程, 思考者实体等
    NoHealthBar,           // 不显示血条(思考者)
    Frozen,                // 冰封: 完全停滞(保留位用于未来扩展)

    Count_                 // 哨兵值
};

constexpr std::uint32_t state_bit(ModifierState s) {
    return std::uint32_t{1} << static_cast<std::uint32_t>(s);
}

// 属性贡献的层级. 映射是有意设计为静态的:
// 仅凭属性名称就能确定其 aggregation(聚合)层级.
constexpr PropertyLayer layer_of(ModifierProperty p) {
    switch (p) {
        case ModifierProperty::ArmorBonusPct:
        case ModifierProperty::AttackDamageBonusPct:
        case ModifierProperty::IncomingDamagePct:
        case ModifierProperty::OutgoingDamagePct:
        case ModifierProperty::MoveSpeedBonusPct:
        case ModifierProperty::HealAmpPct:
        case ModifierProperty::Evasion:
        case ModifierProperty::LifestealPct:
        case ModifierProperty::SpellAmplifyPct:
        case ModifierProperty::StatusResistancePct:
        case ModifierProperty::CooldownReductionPct:
            return PropertyLayer::Percentage;
        default:
            return PropertyLayer::Constant;
    }
}

} // namespace dota
