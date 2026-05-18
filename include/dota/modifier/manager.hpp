#pragma once

#include "dota/modifier/enums.hpp"
#include "dota/modifier/modifier.hpp"

#include <memory>
#include <string>
#include <vector>

namespace dota {

class Unit;

// 拥有并驱动附加到单个 Unit 的修饰器.
//
// 所有权模型: vector 内部的 unique_ptr<Modifier>. `attach()` 返回的修饰器句柄
// 在该修饰器的生命周期内保持稳定, 但一旦被移除或过期就会悬空 -- 调用者应优先
// 通过名称查找以获得长期引用.
class ModifierManager {
public:
    explicit ModifierManager(Unit& owner);

    // 添加在其他地方构造的修饰器. 返回非拥有指针.
    Modifier* attach(std::unique_ptr<Modifier> mod);

    // 附加 motion controller. 按 motion_priority_ 抢占既有 MC.
    // 当新修饰器 priority < 既有的最大 priority 时返回 nullptr 表示拒绝挂载.
    Modifier* attach_motion(std::unique_ptr<Modifier> mod);

    // 仅对 motion 修饰器调用 on_motion_tick(dt).
    void advance_motion(double dt);

    // 辅助函数: 一次调用完成构造 + 附加. 插入后调用 on_created().
    template <typename M, typename... Args>
    M* attach_new(Args&&... args) {
        auto up = std::make_unique<M>(owner_, std::forward<Args>(args)...);
        M* raw = up.get();
        attach(std::move(up));
        return raw;
    }

    // 移除具有此名称的第一个修饰器. 如果移除则返回 true.
    bool remove(const std::string& name);
    void remove_all();

    Modifier*       find(const std::string& name);
    const Modifier* find(const std::string& name) const;

    // 查看原始列表(只读). 在单个 tick 期间保持稳定.
    const std::vector<std::unique_ptr<Modifier>>& all() const { return modifiers_; }

    // 将所有修饰器推进 dt; 清除过期的修饰器并触发 on_destroyed.
    void advance(double dt);

    // --- Aggregation(聚合)查询
    std::uint32_t aggregated_states() const;
    bool has_state(ModifierState s) const { return (aggregated_states() & state_bit(s)) != 0; }

    // 返回所有对 `p` 在其声明层级的贡献总和. 对于常量层级, 这是加法求和;
    // 对于百分比层级, 仍然是求和, 调用者将其组合为 (1 + sum).
    double aggregated(ModifierProperty p) const;

    // 便捷函数: 对于同时具有常量和百分比兄弟属性的属性,
    // 返回 `(base + constant_bonuses) * (1 + pct_bonuses)`.
    double apply_stat(ModifierProperty constant, ModifierProperty pct, double base) const;

    // 将 `PreTakeDamageEvent` / `PostTakeDamageEvent` 分发到此单位上的每个修饰器.
    // 返回变异后的事件.
    void dispatch_pre_take_damage(PreTakeDamageEvent& ev);
    void dispatch_post_take_damage(PostTakeDamageEvent& ev);

    // 治疗等效方法.
    void dispatch_pre_take_heal(PreTakeHealEvent& ev);
    void dispatch_post_take_heal(PostTakeHealEvent& ev);

private:
    Unit& owner_;
    std::vector<std::unique_ptr<Modifier>> modifiers_;
};

} // namespace dota
