#pragma once

#include "dota/core/types.hpp"
#include "dota/modifier/modifier.hpp"

#include <cstdint>

namespace dota {

class Unit;

// 伤害管道的输入。收集调用者在伤害事件结算前可以设置的所有参数——
// 类型/数值/标志位，以及用于吸血和反伤记录的攻击者。
// Stage 5 将数学计算从 Unit::apply_damage 中抽离并集中到这里，
// 使得技能、普通攻击和反伤都走同一条路径。
struct DamageInstance {
    Unit*         attacker{nullptr};     // 环境伤害时可能为 null
    Unit*         victim{nullptr};
    DamageType    type{DamageType::Physical};
    double        amount{0.0};
    std::uint32_t flags{0};              // DamageFlag 位掩码
};

// 治疗管道的输入。治疗增强修改器可以放大或打断治疗。
struct HealInstance {
    Unit*   healer{nullptr};
    Unit*   target{nullptr};
    double  amount{0.0};
};

// 运行完整的伤害管道：
//   1. 攻击者的 OutgoingAmp（输出增幅）（除非有 NoSpellAmplification）
//   2. 受害者的 IncomingAmp（承受增幅）（除非有 NoSpellAmplification）
//   3. 修改器的 on_pre_take_damage（护盾 absorption 吸收）
//   4. 魔法免疫短路（除非有 BypassMagicImmune）
//   5. 类型 resistance（抗性）（物理/护甲曲线，魔法/魔抗）
//   6. 扣除生命值
//   7. 修改器的 on_post_take_damage（reflect 反射/反伤、吸血、触发效果）
// 返回实际扣除的生命值。攻击者为 null 时也可安全调用。
double deal_damage(DamageInstance dmg);

// 运行治疗管道：向受害者的修改器派发治疗前事件，应用 HealAmpPct（治疗 amplification 增幅），
// 限制到最大生命值，派发治疗后事件。返回实际恢复的生命值。
double deal_heal(HealInstance heal);

} // namespace dota
