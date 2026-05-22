#pragma once

#include "dota/core/types.hpp"
#include "dota/modifier/modifier.hpp"  // DamageType

#include <cstdint>
#include <vector>

namespace dota {

// 一次普攻在 attacker 这一侧的快照. 由 World::begin_attack 创建并立即派发到
// attacker 上所有 modifier 的 on_attack 钩子, 法球可以在此时认领 record (push
// 自己进 orb_listeners) 以便命中后调用 on_attack_landed / on_attack_fail.
//
// 近战: begin_attack -> 立即 complete_attack (record 走完即销毁).
// 远程: begin_attack -> 生成 TrackingProjectile, 命中回调里 complete_attack;
//        target 中途消失 / Untargetable -> on_attack_fail + record_destroy.
//
// id 全局递增, 用于 lua 端 modifier 在 on_attack 和 on_attack_landed 之间认 record
// (lua self.records[id] = true).
struct AttackRecord {
    EntityId   id            = kInvalidEntityId;
    EntityId   attacker      = kInvalidEntityId;
    EntityId   target        = kInvalidEntityId;
    double     base_damage   = 0.0;       // begin 时锁定 attacker.attack_damage()
    double     bonus_damage  = 0.0;       // PreAttack BonusDamage 累加 (Stage 2)
    DamageType damage_type   = DamageType::Physical; // 法球可改 (Stage 3)
    bool       missed        = false;     // 闪避结果 (complete 时填)
    bool       processed     = false;     // 已 complete (命中或失败), 避免重复
    // 法球认领: on_attack 中将 self push 进来. on_attack_landed/fail 阶段
    // 仅对这个集合派发. 用 Modifier* 而非名字, 因为同一 modifier 可能挂多次
    // (虽然 v1 单 unit 上同名 modifier 只一份, 但保留指针定位最稳).
    std::vector<Modifier*> orb_listeners;
};

} // namespace dota
