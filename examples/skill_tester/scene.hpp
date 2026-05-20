#pragma once

// skill_tester 的"主场景": 一个 caster + 3 dummy. 切英雄 / R 重置 / 调整 dummy
// 都走 rebuild_with_hero(). 该类负责把 LuaState / AbilityRegistry / World 三个
// owning unique_ptr 维持在正确的销毁顺序 (World 先, lua 后).

#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/types.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/script/lua_state.hpp"
#include "dota/tools/hero_catalog.hpp"

#include "render_helpers.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace dota::skill_tester {

// dummy 配置: 3 个 Dire, 不同 magic_resist, 高血不死.
struct DummySpec {
    const char* label;
    Vec2        pos;
    double      magic_resist;
    double      base_armor;
};

inline constexpr DummySpec kDummies[] = {
    {"Dummy MR0%",  { 600.0, -150.0}, 0.00,  0.0},
    {"Dummy MR25%", { 600.0,    0.0}, 0.25,  5.0},
    {"Dummy MR50%", { 600.0,  150.0}, 0.50, 10.0},
};

// 调参面板对 dummy 默认值的覆盖. 仅在 Apply / Reset 时由主循环写入,
// rebuild_with_hero 消费. 三个 dummy 共用同一份 override (各自的 base
// magic_resist / base_armor 仍按 kDummies 区分; 这里只统一 max_health /
// attack_damage 以及附加 MR / 装甲偏移).
struct DummyOverride {
    bool   active        = false;
    double max_health    = 6000.0;
    double attack_damage = 0.0;
    double magic_resist_bonus = 0.0;
    double base_armor_bonus   = 0.0;
};

// Inspector "Add Ability" 下拉的备选项 (跨所有英雄的全部技能).
struct AbilityChoice {
    std::string label;
    std::string name;
    bool        is_passive{false};
};

class Scene {
public:
    explicit Scene(const tools::HeroCatalog& cat);

    void rebuild_with_hero(std::size_t idx);
    void update(double dt);

    World*       world()        { return world_.get(); }
    Unit*        caster() const { return caster_; }
    const std::vector<Unit*>&    dummies() const { return dummies_; }
    const std::vector<Ability*>& caster_abilities() const { return caster_abilities_; }
    std::size_t  hero_index() const { return hero_index_; }
    std::vector<visual::FloatingText>& texts() { return texts_; }

    void set_dummy_override(const DummyOverride& o) { dummy_override_ = o; }
    const DummyOverride& dummy_override() const     { return dummy_override_; }
    const std::vector<AbilityChoice>& ability_choices() const { return ability_choices_; }
    LuaState* lua_state() const { return lua_.get(); }

    Unit* find_unit(EntityId id) const;
    std::vector<Unit*> units() const;

    void sync_caster_abilities();
    Ability* add_ability_to(Unit& unit, const std::string& name);
    bool     remove_ability_at(Unit& unit, std::size_t index);

    // 收集当前所有要绘制的 RenderUnit / RenderProjectile.
    std::vector<visual::RenderUnit>       render_units() const;
    std::vector<visual::RenderProjectile> render_projectiles() const;

private:
    void build_ability_choices();

    const tools::HeroCatalog&        catalog_;
    std::size_t                      hero_index_{0};
    std::unique_ptr<LuaState>        lua_;
    std::unique_ptr<AbilityRegistry> reg_;
    std::unique_ptr<World>           world_;
    Unit*                            caster_{nullptr};
    std::vector<Unit*>               dummies_;
    std::vector<Ability*>            caster_abilities_;
    std::vector<AbilityChoice>       ability_choices_;
    std::vector<visual::FloatingText> texts_;
    DummyOverride                    dummy_override_{};
};

} // namespace dota::skill_tester
