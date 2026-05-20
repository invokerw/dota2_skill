// 技能测试器: 选英雄 -> 选技能 -> 选目标 / 位置 -> 释放, 看效果.
//
// 控制:
//   左侧栏点击切换英雄 (= 重建 World).
//   底部技能槽点击, 或按 1/2/3/4 选中技能, 进入瞄准模式.
//   * UnitTarget: 鼠标 hover 一个 dummy, 左键释放; 距离过远显示红圈.
//   * PointTarget: 鼠标移动时画从 caster 到指针的线 + width / radius 提示, 左键释放.
//   * NoTarget: 进入待确认状态, 再次按数字键 / SPACE / 左键释放.
//   ESC 或右键取消瞄准.
//   R 重置当前英雄, SPACE 暂停 (非瞄准时), ESC 退出 (无瞄准时).

#include "dota/ability/ability.hpp"
#include "dota/ability/behavior.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/scripted.hpp"
#include "dota/projectile/manager.hpp"
#include "dota/projectile/projectile.hpp"
#include "dota/script/lua_state.hpp"
#include "dota/tools/hero_catalog.hpp"

#include "raylib.h"
#include "imgui.h"
#include "rlImGui.h"
#include "visual_common.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace dota;
using dota::tools::HeroCatalog;
using dota::tools::HeroEntry;
using dota::visual::FloatingText;
using dota::visual::RenderProjectile;
using dota::visual::RenderUnit;
using dota::visual::ViewCamera;

namespace {

constexpr int kWindowW = 1280;
constexpr int kWindowH = 720;

// UI 布局参数. 战场区在 [kSidePanelW, kWindowW - kTunePanelW] x
// [0, kWindowH - kAbilityBarH].
constexpr int kSidePanelW = 220;   // 左侧英雄列表面板宽
constexpr int kTunePanelW = 340;   // 右侧 Inspector 面板宽
constexpr int kAbilityBarH = 96;   // 底部技能栏高度
constexpr int kAbilitySlotMax = 4; // 技能槽最大数量, 多余的不显示

std::string data_dir() {
    if (const char* d = std::getenv("DOTA_DATA_DIR")) return d;
    return DOTA_DATA_DIR;
}

// dummy 配置: 3 个 Dire, 不同 magic_resist, 高血不死.
struct DummySpec {
    const char* label;
    Vec2        pos;
    double      magic_resist;
    double      base_armor;
};
constexpr DummySpec kDummies[] = {
    {"Dummy MR0%",  { 600.0, -150.0}, 0.00,  0.0},
    {"Dummy MR25%", { 600.0,    0.0}, 0.25,  5.0},
    {"Dummy MR50%", { 600.0,  150.0}, 0.50, 10.0},
};

// S5 调参覆盖: 仅在 Apply / Reset 时由 main 写入, rebuild_with_hero 消费.
// 三个 dummy 共用同一份 override (各自的 magic_resist / base_armor 还是按
// kDummies 区分; 这里只统一 max_health / attack_damage / 以及"额外 MR / 装甲
// 偏移"). 第一版保持简单.
struct DummyOverride {
    bool   active        = false;
    double max_health    = 6000.0;
    double attack_damage = 0.0;
    double magic_resist_bonus = 0.0;   // 加到每个 dummy 的 base MR 上
    double base_armor_bonus   = 0.0;
};

struct AbilityChoice {
    std::string label;
    std::string name;
    bool        is_passive{false};
};

// 主场景: 一个 caster + 3 dummy. 切英雄 / R 都走 rebuild_with_hero().
class Scene {
public:
    explicit Scene(const HeroCatalog& cat) : catalog_(cat) {
        build_ability_choices();
        rebuild_with_hero(0);
    }

    void rebuild_with_hero(std::size_t idx) {
        if (idx >= catalog_.heroes().size()) return;
        hero_index_ = idx;

        // 销毁顺序: World 持有的 ScriptedAbility 内部 sol::table / sol::function
        // 引用了 lua_ 拥有的 lua_State. 必须先销毁 world_ (连带 reg_), 再销毁
        // lua_, 否则 sol 析构时会对已销毁的 state 调 luaL_unref 触发 segfault.
        world_.reset();
        reg_.reset();
        lua_.reset();

        lua_   = std::make_unique<LuaState>();
        reg_   = std::make_unique<AbilityRegistry>();
        reg_->set_lua(lua_.get());
        // Inspector 允许给任意单位添加任意已注册技能, 因此每次重建都加载全部 hero YAML.
        for (const auto& hero : catalog_.heroes()) {
            reg_->load_file(hero.yaml_path);
        }

        world_  = std::make_unique<World>();
        texts_.clear();

        const HeroEntry& h = catalog_.heroes()[hero_index_];

        UnitStats cs;
        cs.max_health       = h.base_health > 0 ? h.base_health : 800.0;
        cs.max_mana         = h.base_mana   > 0 ? h.base_mana   : 600.0;
        cs.attack_damage    = 55.0;
        cs.base_armor       = h.base_armor;
        cs.magic_resist     = h.magic_resist;
        cs.hull_radius      = h.hull_radius;
        cs.base_attack_time = 1.7;
        caster_ = world_->spawn(h.yaml_name, Team::Radiant, cs, {-600.0, 0.0});

        // 给 caster 实例化所有非 passive 技能, 用于后续选择 / 施法
        caster_abilities_.clear();
        for (const auto& a : h.abilities) {
            if (a.is_passive) continue;
            Ability* inst = reg_->instantiate(a.name, *caster_);
            if (inst) caster_abilities_.push_back(inst);
        }
        sync_caster_abilities();

        // 3 个 dummy. 若 dummy_override_.active, 用调参面板的 stats.
        dummies_.clear();
        for (const auto& d : kDummies) {
            UnitStats ds;
            ds.max_health    = dummy_override_.active
                ? dummy_override_.max_health : 6000.0;
            ds.max_mana      = 0.0;
            ds.attack_damage = dummy_override_.active
                ? dummy_override_.attack_damage : 0.0;
            ds.base_armor    = d.base_armor +
                (dummy_override_.active ? dummy_override_.base_armor_bonus : 0.0);
            ds.magic_resist  = d.magic_resist +
                (dummy_override_.active ? dummy_override_.magic_resist_bonus : 0.0);
            ds.base_attack_time = 1.7;
            dummies_.push_back(world_->spawn(d.label, Team::Dire, ds, d.pos));
        }

        // 飘字订阅
        world_->events().subscribe<DamageAppliedEvent>(
            [this](DamageAppliedEvent& e) {
                if (e.amount_applied <= 0.0) return;
                Unit* v = world_->find(e.victim);
                if (!v) return;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "-%.0f", e.amount_applied);
                Color c = (e.type == DamageType::Magical) ? Color{120, 180, 255, 255}
                        : (e.type == DamageType::Pure)    ? Color{255, 220, 120, 255}
                                                          : Color{255, 100, 100, 255};
                texts_.push_back({v->position(), buf, c, world_->time()});
            });
        world_->events().subscribe<HealAppliedEvent>(
            [this](HealAppliedEvent& e) {
                if (e.amount <= 0.0) return;
                Unit* v = world_->find(e.target);
                if (!v) return;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "+%.0f", e.amount);
                texts_.push_back({v->position(), buf, Color{120, 230, 120, 255},
                                 world_->time()});
            });
    }

    void update(double dt) {
        if (!world_) return;
        world_->advance(dt);
        const double now = world_->time();
        texts_.erase(
            std::remove_if(texts_.begin(), texts_.end(),
                [now](const FloatingText& f) { return now - f.spawn_time > 1.2; }),
            texts_.end());
    }

    World*                            world()        { return world_.get(); }
    Unit*                             caster() const { return caster_; }
    const std::vector<Unit*>&         dummies() const { return dummies_; }
    const std::vector<Ability*>&      caster_abilities() const { return caster_abilities_; }
    std::size_t                       hero_index() const { return hero_index_; }
    std::vector<FloatingText>&        texts()        { return texts_; }

    void set_dummy_override(const DummyOverride& o) { dummy_override_ = o; }
    const DummyOverride& dummy_override() const     { return dummy_override_; }
    const std::vector<AbilityChoice>& ability_choices() const { return ability_choices_; }
    LuaState* lua_state() const { return lua_.get(); }

    Unit* find_unit(EntityId id) const {
        return world_ ? world_->find(id) : nullptr;
    }

    std::vector<Unit*> units() const {
        std::vector<Unit*> out;
        if (!world_) return out;
        for (Team t : {Team::Radiant, Team::Dire, Team::Neutral}) {
            for (Unit* u : world_->units_on_team(t)) {
                if (u) out.push_back(u);
            }
        }
        return out;
    }

    void sync_caster_abilities() {
        caster_abilities_.clear();
        if (!caster_) return;
        for (const auto& a : caster_->abilities().all()) {
            if (a && !a->is_passive()) caster_abilities_.push_back(a.get());
        }
    }

    Ability* add_ability_to(Unit& unit, const std::string& name) {
        if (!reg_) return nullptr;
        Ability* ab = reg_->instantiate(name, unit);
        if (ab && &unit == caster_) sync_caster_abilities();
        return ab;
    }

    bool remove_ability_at(Unit& unit, std::size_t index) {
        const bool ok = unit.abilities().remove_at(index);
        if (ok && &unit == caster_) sync_caster_abilities();
        return ok;
    }

    // 收集当前所有要绘制的 RenderUnit / RenderProjectile (复用 visual_common)
    std::vector<RenderUnit> render_units() const {
        std::vector<RenderUnit> out;
        if (!world_) return out;
        for (Team t : {Team::Radiant, Team::Dire, Team::Neutral}) {
            for (Unit* u : world_->units_on_team(t)) {
                if (!u) continue;
                RenderUnit ru;
                ru.id      = u->id();
                ru.name    = u->name();
                ru.team    = u->team();
                ru.alive   = u->alive();
                ru.hp      = u->health();
                ru.max_hp  = u->max_health();
                ru.position= u->position();
                ru.hull_radius = u->hull_radius();
                for (auto& m : u->modifiers().all()) ru.modifiers.push_back(m->name());
                for (const auto& a : u->abilities().all()) {
                    if (a->phase() != CastPhase::Casting) continue;
                    const float total = static_cast<float>(a->cast_point());
                    if (total <= 0.0f) continue;
                    ru.casting_ability = a->name();
                    ru.cast_progress = 1.0f - std::clamp(
                        static_cast<float>(a->phase_timer()) / total, 0.0f, 1.0f);
                    break;
                }
                out.push_back(std::move(ru));
            }
        }
        return out;
    }
    std::vector<RenderProjectile> render_projectiles() const {
        std::vector<RenderProjectile> out;
        if (!world_) return out;
        for (const auto& p : world_->projectiles().live()) {
            if (!p) continue;
            RenderProjectile rp;
            rp.pid    = p->pid();
            rp.pos    = p->position();
            rp.linear = p->is_linear();
            rp.dir    = p->direction();
            rp.width  = p->width();
            out.push_back(rp);
        }
        return out;
    }

private:
    void build_ability_choices() {
        ability_choices_.clear();
        for (const auto& hero : catalog_.heroes()) {
            for (const auto& ability : hero.abilities) {
                AbilityChoice choice;
                choice.name = ability.name;
                choice.is_passive = ability.is_passive;
                choice.label = hero.yaml_name + " / " + ability.name;
                if (ability.is_passive) choice.label += " (passive)";
                ability_choices_.push_back(std::move(choice));
            }
        }
    }

    const HeroCatalog&              catalog_;
    std::size_t                     hero_index_{0};
    std::unique_ptr<LuaState>       lua_;
    std::unique_ptr<AbilityRegistry> reg_;
    std::unique_ptr<World>          world_;
    Unit*                           caster_{nullptr};
    std::vector<Unit*>              dummies_;
    std::vector<Ability*>           caster_abilities_;
    std::vector<AbilityChoice>      ability_choices_;
    std::vector<FloatingText>       texts_;
    DummyOverride                   dummy_override_{};
};

} // namespace

// 把 ability behavior 翻成短标签, 显示在按钮上.
const char* behavior_label(std::uint32_t b) {
    if (has_flag(b, BehaviorFlag::Channelled))   return "CHN";
    if (has_flag(b, BehaviorFlag::PointTarget))  return "PT";
    if (has_flag(b, BehaviorFlag::UnitTarget))   return "UT";
    if (has_flag(b, BehaviorFlag::NoTarget))     return "NT";
    return "?";
}

// --- S4: 瞄准状态 ---
enum class AimMode {
    None,
    AwaitUnitTarget,
    AwaitPointTarget,
    AwaitConfirmNoTarget,
};

AimMode aim_for_behavior(std::uint32_t b) {
    if (has_flag(b, BehaviorFlag::PointTarget)) return AimMode::AwaitPointTarget;
    if (has_flag(b, BehaviorFlag::UnitTarget))  return AimMode::AwaitUnitTarget;
    if (has_flag(b, BehaviorFlag::NoTarget))    return AimMode::AwaitConfirmNoTarget;
    return AimMode::None;
}

const char* cast_error_text(CastError e) {
    switch (e) {
        case CastError::None:              return "OK";
        case CastError::NotReady:          return "Not ready";
        case CastError::OnCooldown:        return "On cooldown";
        case CastError::NotEnoughMana:     return "Not enough mana";
        case CastError::Silenced:          return "Silenced";
        case CastError::Stunned:           return "Stunned";
        case CastError::Hexed:             return "Hexed";
        case CastError::CasterDead:        return "Caster dead";
        case CastError::InvalidTarget:     return "Invalid target";
        case CastError::TargetMagicImmune: return "Target magic immune";
        case CastError::OutOfRange:        return "Out of range";
        case CastError::NotLearned:        return "Not learned";
    }
    return "?";
}

// 解析当前等级下的 special 字段值; 不存在返回 NaN.
double special_or_nan(const Ability& ab, const char* key) {
    const auto& sp = ab.ability_special();
    auto it = sp.find(key);
    if (it == sp.end()) return std::numeric_limits<double>::quiet_NaN();
    return it->second.get_float(ab.level());
}

// AoE 预览 (圆形): 优先 radius. 找不到给默认.
double preview_aoe_radius(const Ability& ab, double fallback) {
    const double r = special_or_nan(ab, "radius");
    return std::isnan(r) ? fallback : r;
}

// 线性投射物预览参数. 没有 width/length 时返回 false.
struct LinearPreview { double length; double width; };
bool preview_linear(const Ability& ab, LinearPreview& out) {
    const double w = special_or_nan(ab, "width");
    const double l = special_or_nan(ab, "length");
    if (std::isnan(w) || std::isnan(l)) return false;
    out.width = w; out.length = l;
    return true;
}

double dist2(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

const char* team_label(Team t) {
    switch (t) {
        case Team::Radiant: return "Radiant";
        case Team::Dire:    return "Dire";
        case Team::Neutral: return "Neutral";
    }
    return "?";
}

const char* state_label(ModifierState s) {
    switch (s) {
        case ModifierState::Stunned:         return "Stunned";
        case ModifierState::Silenced:        return "Silenced";
        case ModifierState::Rooted:          return "Rooted";
        case ModifierState::Disarmed:        return "Disarmed";
        case ModifierState::Hexed:           return "Hexed";
        case ModifierState::Invisible:       return "Invisible";
        case ModifierState::Invulnerable:    return "Invulnerable";
        case ModifierState::OutOfGame:       return "OutOfGame";
        case ModifierState::MagicImmune:     return "MagicImmune";
        case ModifierState::Untargetable:    return "Untargetable";
        case ModifierState::NoUnitCollision: return "NoUnitCollision";
        case ModifierState::NoHealthBar:     return "NoHealthBar";
        case ModifierState::Frozen:          return "Frozen";
        case ModifierState::Count_:          break;
    }
    return "?";
}

const char* property_label(ModifierProperty p) {
    switch (p) {
        case ModifierProperty::ArmorBonus:               return "ArmorBonus";
        case ModifierProperty::ArmorBonusPct:            return "ArmorBonusPct";
        case ModifierProperty::HealthBonus:              return "HealthBonus";
        case ModifierProperty::ManaBonus:                return "ManaBonus";
        case ModifierProperty::AttackDamageBonus:        return "AttackDamageBonus";
        case ModifierProperty::AttackDamageBonusPct:     return "AttackDamageBonusPct";
        case ModifierProperty::AttackSpeedBonusConstant: return "AttackSpeedBonus";
        case ModifierProperty::MagicResistBonus:         return "MagicResistBonus";
        case ModifierProperty::IncomingDamagePct:        return "IncomingDamagePct";
        case ModifierProperty::OutgoingDamagePct:        return "OutgoingDamagePct";
        case ModifierProperty::MoveSpeedBonusConstant:   return "MoveSpeedBonus";
        case ModifierProperty::MoveSpeedBonusPct:        return "MoveSpeedBonusPct";
        case ModifierProperty::HealAmpPct:               return "HealAmpPct";
        case ModifierProperty::Evasion:                  return "Evasion";
        case ModifierProperty::LifestealPct:             return "LifestealPct";
        case ModifierProperty::HealthRegen:              return "HealthRegen";
        case ModifierProperty::ManaRegen:                return "ManaRegen";
        case ModifierProperty::SpellAmplifyPct:          return "SpellAmplifyPct";
        case ModifierProperty::StatusResistancePct:      return "StatusResistancePct";
        case ModifierProperty::CooldownReductionPct:     return "CooldownReductionPct";
        case ModifierProperty::CastRangeBonus:           return "CastRangeBonus";
        case ModifierProperty::Count_:                   break;
    }
    return "?";
}

const char* phase_label(CastPhase p) {
    switch (p) {
        case CastPhase::Ready:       return "Ready";
        case CastPhase::Casting:     return "Casting";
        case CastPhase::Backswing:   return "Backswing";
        case CastPhase::Channelling: return "Channelling";
        case CastPhase::OnCooldown:  return "OnCooldown";
    }
    return "?";
}

bool drag_double(const char* label, double& value, float speed,
                 double min_v, double max_v, const char* fmt) {
    float f = static_cast<float>(value);
    if (!ImGui::DragFloat(label, &f, speed,
                          static_cast<float>(min_v),
                          static_cast<float>(max_v), fmt)) {
        return false;
    }
    value = static_cast<double>(f);
    return true;
}

void draw_state_mask(std::uint32_t mask) {
    bool any = false;
    for (int i = 0; i < static_cast<int>(ModifierState::Count_); ++i) {
        const auto state = static_cast<ModifierState>(i);
        if ((mask & state_bit(state)) == 0) continue;
        if (any) ImGui::SameLine();
        ImGui::TextUnformatted(state_label(state));
        any = true;
    }
    if (!any) ImGui::TextDisabled("(none)");
}

enum class ModifierParamKind {
    Number,
    Int,
    Property,
    Vec2,
};

struct ModifierParamSpec {
    std::string       key;
    std::string       label;
    ModifierParamKind kind{ModifierParamKind::Number};
    double            number_default{0.0};
    double            min{0.0};
    double            max{0.0};
    float             speed{0.1f};
    const char*       format{"%.2f"};
    int               int_default{0};
    int               int_min{0};
    int               int_max{0};
    Vec2              vec_default{0.0, 0.0};
};

struct ModifierParamValue {
    double number{0.0};
    int    integer{0};
    int    property_index{0};
    Vec2   vec{0.0, 0.0};
};

using ModifierParamBag = std::unordered_map<std::string, ModifierParamValue>;
using ModifierAddFactory =
    std::function<std::unique_ptr<Modifier>(Unit&, const ModifierParamBag&)>;

struct ModifierAddSpec {
    std::string               name;
    std::string               label;
    std::vector<ModifierParamSpec> params;
    ModifierAddFactory        create;
};

ModifierParamSpec number_param(std::string key, std::string label,
                               double def, double min_v, double max_v,
                               float speed, const char* format) {
    ModifierParamSpec out;
    out.key = std::move(key);
    out.label = std::move(label);
    out.kind = ModifierParamKind::Number;
    out.number_default = def;
    out.min = min_v;
    out.max = max_v;
    out.speed = speed;
    out.format = format;
    return out;
}

ModifierParamSpec int_param(std::string key, std::string label,
                            int def, int min_v, int max_v, float speed) {
    ModifierParamSpec out;
    out.key = std::move(key);
    out.label = std::move(label);
    out.kind = ModifierParamKind::Int;
    out.int_default = def;
    out.int_min = min_v;
    out.int_max = max_v;
    out.speed = speed;
    return out;
}

ModifierParamSpec property_param(std::string key, std::string label,
                                 ModifierProperty def) {
    ModifierParamSpec out;
    out.key = std::move(key);
    out.label = std::move(label);
    out.kind = ModifierParamKind::Property;
    out.int_default = static_cast<int>(def);
    return out;
}

ModifierParamSpec vec2_param(std::string key, std::string label,
                             Vec2 def, double min_v, double max_v,
                             float speed, const char* format) {
    ModifierParamSpec out;
    out.key = std::move(key);
    out.label = std::move(label);
    out.kind = ModifierParamKind::Vec2;
    out.vec_default = def;
    out.min = min_v;
    out.max = max_v;
    out.speed = speed;
    out.format = format;
    return out;
}

std::vector<ModifierParamSpec> common_modifier_params() {
    return {
        number_param("duration", "Duration", 3.0, -1.0, 600.0, 0.05f, "%.1f"),
        int_param("stacks", "Stacks", 1, 1, 999, 0.1f),
    };
}

std::vector<ModifierParamSpec>
with_common_params(std::initializer_list<ModifierParamSpec> extra) {
    auto out = common_modifier_params();
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
}

void reset_modifier_param_values(const ModifierAddSpec& spec, ModifierParamBag& values) {
    values.clear();
    for (const auto& param : spec.params) {
        ModifierParamValue value;
        value.number = param.number_default;
        value.integer = param.int_default;
        value.property_index = param.int_default;
        value.vec = param.vec_default;
        values.emplace(param.key, value);
    }
}

double param_number(const ModifierParamBag& params, const std::string& key, double fallback) {
    const auto it = params.find(key);
    return it == params.end() ? fallback : it->second.number;
}

int param_int(const ModifierParamBag& params, const std::string& key, int fallback) {
    const auto it = params.find(key);
    return it == params.end() ? fallback : it->second.integer;
}

ModifierProperty param_property(const ModifierParamBag& params, const std::string& key,
                                ModifierProperty fallback) {
    const auto it = params.find(key);
    const int idx = it == params.end()
        ? static_cast<int>(fallback)
        : it->second.property_index;
    return static_cast<ModifierProperty>(
        std::clamp(idx, 0, static_cast<int>(ModifierProperty::Count_) - 1));
}

Vec2 param_vec2(const ModifierParamBag& params, const std::string& key, Vec2 fallback) {
    const auto it = params.find(key);
    return it == params.end() ? fallback : it->second.vec;
}

std::unique_ptr<Modifier>
finish_modifier(std::unique_ptr<Modifier> mod, const ModifierParamBag& params) {
    if (!mod) return nullptr;
    const int stacks = std::max(1, param_int(params, "stacks", 1));
    if (stacks > 1) mod->set_stack_count(stacks);
    return mod;
}

void draw_modifier_param_controls(const ModifierAddSpec& spec, ModifierParamBag& values) {
    for (const auto& param : spec.params) {
        ModifierParamValue& value = values[param.key];
        switch (param.kind) {
            case ModifierParamKind::Number:
                drag_double(param.label.c_str(), value.number,
                            param.speed, param.min, param.max, param.format);
                break;
            case ModifierParamKind::Int:
                ImGui::DragInt(param.label.c_str(), &value.integer,
                               param.speed, param.int_min, param.int_max);
                value.integer = std::clamp(value.integer, param.int_min, param.int_max);
                break;
            case ModifierParamKind::Property: {
                value.property_index = std::clamp(
                    value.property_index, 0,
                    static_cast<int>(ModifierProperty::Count_) - 1);
                const auto current = static_cast<ModifierProperty>(value.property_index);
                if (ImGui::BeginCombo(param.label.c_str(), property_label(current))) {
                    for (int i = 0; i < static_cast<int>(ModifierProperty::Count_); ++i) {
                        const bool selected_prop = value.property_index == i;
                        const auto prop = static_cast<ModifierProperty>(i);
                        if (ImGui::Selectable(property_label(prop), selected_prop)) {
                            value.property_index = i;
                        }
                        if (selected_prop) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case ModifierParamKind::Vec2:
                ImGui::PushID(param.key.c_str());
                ImGui::TextUnformatted(param.label.c_str());
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f - 4.0f);
                drag_double("X", value.vec.x, param.speed, param.min, param.max, param.format);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                drag_double("Y", value.vec.y, param.speed, param.min, param.max, param.format);
                ImGui::PopID();
                break;
        }
    }
}

std::vector<ModifierAddSpec> build_builtin_modifier_specs() {
    std::vector<ModifierAddSpec> out;
    auto push = [&](std::string name,
                    std::vector<ModifierParamSpec> params,
                    ModifierAddFactory create) {
        out.push_back({name, "c++ / " + name, std::move(params), std::move(create)});
    };

    push("modifier_stunned", common_modifier_params(),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_stunned(unit, param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_silenced", common_modifier_params(),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_silenced(unit, param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_rooted", common_modifier_params(),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_rooted(unit, param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_hexed", common_modifier_params(),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_hexed(unit, param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_invisible", common_modifier_params(),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_invisible(unit, param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_magic_immune", common_modifier_params(),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_magic_immune(unit, param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_debug_stats",
         with_common_params({
             property_param("property", "Property", ModifierProperty::AttackDamageBonus),
             number_param("value", "Value", 25.0, -10000.0, 10000.0, 0.1f, "%.2f"),
         }),
         [](Unit& unit, const ModifierParamBag& params) {
             const auto prop = param_property(
                 params, "property", ModifierProperty::AttackDamageBonus);
             return finish_modifier(
                 std::make_unique<modifiers::GenericStats>(
                     unit, "modifier_debug_stats",
                     param_number(params, "duration", 3.0),
                     std::initializer_list<ModifierProvidedProperty>{
                         {prop, param_number(params, "value", 25.0)}}),
                 params);
         });
    push("modifier_shield_absorb",
         with_common_params({
             number_param("capacity", "Capacity", 300.0, 1.0, 100000.0, 5.0f, "%.0f"),
         }),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 std::make_unique<modifiers::ShieldAbsorb>(
                     unit, param_number(params, "capacity", 300.0),
                     param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_periodic_heal",
         with_common_params({
             number_param("heal_per_tick", "Heal/Tick", 50.0,
                          -10000.0, 10000.0, 1.0f, "%.0f"),
             number_param("interval", "Interval", 1.0, 0.01, 60.0, 0.05f, "%.2f"),
         }),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_periodic_heal(
                     unit, param_number(params, "heal_per_tick", 50.0),
                     std::max(0.01, param_number(params, "interval", 1.0)),
                     param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_reflect_damage",
         with_common_params({
             number_param("fraction", "Reflect", 0.5, 0.0, 10.0, 0.01f, "%.2f"),
         }),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_blade_mail(
                     unit, param_number(params, "fraction", 0.5),
                     param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_motion_knockback",
         with_common_params({
             vec2_param("direction", "Direction", {1.0, 0.0}, -1.0, 1.0, 0.05f, "%.2f"),
             number_param("distance", "Distance", 300.0, 0.0, 5000.0, 5.0f, "%.0f"),
             int_param("priority", "Priority", 1, -100, 100, 0.1f),
         }),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_knockback(
                     unit, param_vec2(params, "direction", {1.0, 0.0}),
                     param_number(params, "distance", 300.0),
                     param_number(params, "duration", 3.0),
                     param_int(params, "priority", 1)),
                 params);
         });
    push("modifier_break_healing",
         with_common_params({
             number_param("fraction", "Heal Break", 0.4, 0.0, 1.0, 0.01f, "%.2f"),
         }),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_break_healing(
                     unit, param_number(params, "fraction", 0.4),
                     param_number(params, "duration", 3.0)),
                 params);
         });

    return out;
}

std::vector<ModifierAddSpec> build_modifier_catalog(Scene& scene) {
    std::vector<ModifierAddSpec> out = build_builtin_modifier_specs();
    LuaState* lua = scene.lua_state();
    if (!lua) return out;

    for (const std::string& name : lua->modifier_registry().names()) {
        out.push_back({
            name,
            "lua / " + name,
            common_modifier_params(),
            [lua, name](Unit& unit, const ModifierParamBag& params) {
                const auto* spec = lua->modifier_registry().find(name);
                if (!spec) return std::unique_ptr<Modifier>{};
                return finish_modifier(
                    std::make_unique<ScriptedModifier>(
                        unit, name, param_number(params, "duration", 3.0),
                        *spec, *lua),
                    params);
            },
        });
    }
    return out;
}

int main() {
    HeroCatalog catalog;
    try {
        catalog.scan(data_dir() + "/heroes");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "扫描英雄目录失败: %s\n", e.what());
        return 1;
    }
    if (catalog.heroes().empty()) {
        std::fprintf(stderr, "data/heroes/ 下没有可用英雄\n");
        return 1;
    }

    InitWindow(kWindowW, kWindowH, "dota2_skill -- skill tester");
    SetTargetFPS(60);
    // 我们自己处理 ESC: 优先取消瞄准, 其次退出. 不让 raylib 直接 set close.
    SetExitKey(KEY_NULL);

    rlImGuiSetup(/*dark theme=*/true);

    Scene scene(catalog);
    EntityId selected_unit_id = scene.caster() ? scene.caster()->id() : kInvalidEntityId;
    auto select_caster = [&] {
        selected_unit_id = scene.caster() ? scene.caster()->id() : kInvalidEntityId;
    };

    // 战场视图: 居中到 [kSidePanelW, kWindowW - kTunePanelW] x
    // [0, kWindowH - kAbilityBarH].
    ViewCamera cam;
    const int field_x0 = kSidePanelW;
    const int field_x1 = kWindowW - kTunePanelW;
    const int field_y0 = 0;
    const int field_y1 = kWindowH - kAbilityBarH;
    cam.window_w = field_x1 - field_x0;
    cam.window_h = field_y1 - field_y0;
    cam.origin_x = static_cast<float>(field_x0) + cam.window_w * 0.5f;
    cam.origin_y = static_cast<float>(field_y0) + cam.window_h * 0.5f;

    int  hero_active  = 0;
    int  selected_ability = -1;
    AimMode aim = AimMode::None;
    bool paused = false;

    // Stage 4: dummy AI 模式. 每帧推进 -- Idle 不动, Strafe 上下来回,
    // Charge 持续追击 caster (wall trace 自动避障).
    enum class DummyAI { Idle = 0, Strafe = 1, Charge = 2 };
    int dummy_ai_idx = 0;
    // Strafe 状态: +1 表示朝 +Y bookend, -1 朝 -Y. 与 dummies 索引对齐.
    std::vector<int> strafe_dir = {1, 1, 1};

    // S5: 调参面板状态. 用 float 跟 imgui 滑动条绑定.
    float tune_max_health  = 6000.0f;
    float tune_attack_dmg  = 0.0f;
    float tune_mr_bonus    = 0.0f;     // -1.0..+1.0
    float tune_armor_bonus = 0.0f;     // -10..+30
    auto apply_dummy_tune = [&](bool also_rebuild) {
        DummyOverride o;
        o.active             = true;
        o.max_health         = std::max(1.0f, tune_max_health);
        o.attack_damage      = std::max(0.0f, tune_attack_dmg);
        o.magic_resist_bonus = tune_mr_bonus;
        o.base_armor_bonus   = tune_armor_bonus;
        scene.set_dummy_override(o);
        if (also_rebuild) {
            scene.rebuild_with_hero(scene.hero_index());
            select_caster();
            selected_ability = -1;
            aim = AimMode::None;
        }
    };
    std::string toast_text;     // 顶部 toast (1.5s 淡出)
    double      toast_t0 = -10.0;
    Color       toast_color = Color{255, 200, 80, 255};
    auto show_toast = [&](const std::string& s, Color c) {
        toast_text = s;
        toast_t0   = scene.world()->time();
        toast_color = c;
    };

    auto reset_aim = [&] { aim = AimMode::None; };

    // 把 caster_abilities_ 里的 Ability* 反查为 caster->abilities().all() 中的下标 --
    // OrderCast* variant 用的是后者的 index (含 passive 槽).
    auto ability_index_of = [&](Ability* ab) -> int {
        if (!scene.caster() || !ab) return -1;
        const auto& all = scene.caster()->abilities().all();
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i].get() == ab) return static_cast<int>(i);
        }
        return -1;
    };

    // 通过指令队列发起施法. 距离不够时单位会自动靠近, 而非弹 OutOfRange toast.
    // 仍然先调 can_cast 一次, 把"魔不够 / 已死 / cooldown / silence"等本地可判
    // 定的失败抛 toast -- 这些不该派生跟随移动.
    auto try_cast = [&](Ability* ab, const CastTarget& tgt) {
        const CastError pre = ab->can_cast(tgt);
        // OutOfRange / InvalidTarget(纯距离派生上来的) 留给 OrderQueue 处理.
        if (pre != CastError::None && pre != CastError::OutOfRange) {
            show_toast(std::string(cast_error_text(pre)),
                       Color{220, 100, 100, 255});
            reset_aim();
            return;
        }
        const int idx = ability_index_of(ab);
        if (idx < 0) { reset_aim(); return; }
        Unit* caster = scene.caster();
        if (!caster) { reset_aim(); return; }
        if (tgt.unit) {
            caster->issue_order(OrderCastTarget{idx, tgt.unit->id()});
        } else if (tgt.has_point) {
            caster->issue_order(OrderCastPoint{idx, tgt.point});
        } else {
            caster->issue_order(OrderCastNoTarget{idx});
        }
        show_toast("Cast: " + ab->name(),
                   Color{120, 230, 120, 255});
        reset_aim();
    };

    bool quit = false;
    while (!quit && !WindowShouldClose()) {
        // imgui 在每帧开始时消费 raylib 事件, 之后通过 io.WantCapture* 告诉我们
        // 输入是否被 GUI 截获. 必须在 rlImGuiBegin 之后再 query.
        rlImGuiBegin();
        const ImGuiIO& io = ImGui::GetIO();
        const bool gui_wants_mouse    = io.WantCaptureMouse;
        const bool gui_wants_keyboard = io.WantCaptureKeyboard;

        // ESC: 优先取消瞄准, 没在瞄准时退出窗口.
        if (!gui_wants_keyboard && IsKeyPressed(KEY_ESCAPE)) {
            if (aim != AimMode::None) {
                reset_aim();
            } else {
                quit = true;
            }
        }

        if (!gui_wants_keyboard && aim == AimMode::None && IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (!gui_wants_keyboard && IsKeyPressed(KEY_R)) {
            scene.rebuild_with_hero(scene.hero_index());
            select_caster();
            selected_ability = -1;
            reset_aim();
            paused = false;
        }
        // S: 全停 -- 清空 caster 的指令队列(Dota 风格 stop). 不打断当前已经
        // 进入 cast point 的 ability(Stage 3 验证); 仅清掉队列里待派发项.
        if (!gui_wants_keyboard && IsKeyPressed(KEY_S) &&
            scene.caster() && scene.caster()->alive()) {
            scene.caster()->issue_order(OrderStop{});
            reset_aim();
        }

        // 数字键 1-4 选中技能槽: 第一次按 = 选并进入瞄准; 已在该槽瞄准时, 对
        // NoTarget 触发释放, 其他类型切回选择 (无变化).
        const int key_slots[] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR};
        const int slot_count =
            std::min<int>(kAbilitySlotMax,
                          static_cast<int>(scene.caster_abilities().size()));
        for (int i = 0; i < slot_count && !gui_wants_keyboard; ++i) {
            if (!IsKeyPressed(key_slots[i])) continue;
            Ability* ab = scene.caster_abilities()[i];
            const AimMode want = aim_for_behavior(ab->behavior());
            if (selected_ability == i && aim == AimMode::AwaitConfirmNoTarget) {
                // 已选 NoTarget 槽再按一次 = 释放.
                CastTarget tgt;
                try_cast(ab, tgt);
                selected_ability = -1;
            } else {
                selected_ability = i;
                aim = want;
            }
        }

        // SPACE 在 NoTarget 待确认时也可释放
        if (!gui_wants_keyboard &&
            aim == AimMode::AwaitConfirmNoTarget && IsKeyPressed(KEY_SPACE) &&
            selected_ability >= 0 && selected_ability < slot_count) {
            CastTarget tgt;
            try_cast(scene.caster_abilities()[selected_ability], tgt);
            selected_ability = -1;
        }

        // 鼠标位置 -> 世界坐标; 仅当鼠标在战场区且未被 GUI 截获时有效
        const Vector2 ms = GetMousePosition();
        const bool mouse_in_field =
            !gui_wants_mouse &&
            ms.x >= field_x0 && ms.x < field_x1 &&
            ms.y >= field_y0 && ms.y < field_y1;
        const Vec2 mouse_world = cam.to_world(ms);

        // 拾取最近的活着的 dummy. 拾取半径 = 单位 hull_radius, 但保证最小屏幕半径
        // 不低于 kMinUnitRadiusPx 等价的世界距离, 避免 zoom 过大时点不到.
        Unit* hover_unit = nullptr;
        Unit* inspect_hover_unit = nullptr;
        if (mouse_in_field) {
            const double min_world_r = dota::visual::kMinUnitRadiusPx / cam.zoom;
            for (Unit* u : scene.dummies()) {
                if (!u || !u->alive()) continue;
                const double pick_r = std::max(u->hull_radius(), min_world_r);
                if (dist2(u->position(), mouse_world) <= pick_r * pick_r) {
                    hover_unit = u;
                    break;
                }
            }
            double best_d2 = std::numeric_limits<double>::max();
            for (Unit* u : scene.units()) {
                if (!u) continue;
                const double pick_r = std::max(u->hull_radius(), min_world_r);
                const double d2 = dist2(u->position(), mouse_world);
                if (d2 <= pick_r * pick_r && d2 < best_d2) {
                    best_d2 = d2;
                    inspect_hover_unit = u;
                }
            }
        }

        // 右键取消瞄准
        if (!gui_wants_mouse && aim != AimMode::None &&
            IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            reset_aim();
        }

        // 非 aim 模式下 RMB 命令 caster 走位
        if (mouse_in_field && aim == AimMode::None &&
            IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) &&
            scene.caster() && scene.caster()->alive()) {
            scene.caster()->issue_move(mouse_world);
        }

        // 非 aim 模式下 LMB 选中任意单位, 右侧 Inspector 显示其状态.
        if (mouse_in_field && aim == AimMode::None &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && inspect_hover_unit) {
            selected_unit_id = inspect_hover_unit->id();
        }

        // 左键 -- 仅在战场内有效
        if (mouse_in_field && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            selected_ability >= 0 && selected_ability < slot_count) {
            Ability* ab = scene.caster_abilities()[selected_ability];
            CastTarget tgt;
            bool fire = false;
            if (aim == AimMode::AwaitUnitTarget && hover_unit) {
                tgt.unit = hover_unit;
                fire = true;
            } else if (aim == AimMode::AwaitPointTarget) {
                tgt.point = mouse_world;
                tgt.has_point = true;
                fire = true;
            } else if (aim == AimMode::AwaitConfirmNoTarget) {
                fire = true;
            }
            if (fire) {
                try_cast(ab, tgt);
                selected_ability = -1;
            }
        }

        const float dt_raw = GetFrameTime();
        const double dt = paused ? 0.0 : std::min(static_cast<double>(dt_raw), 0.05);

        // Dummy AI 在 scene.update 之前推进 -- 命令在本 tick 即生效.
        if (dt > 0.0 && dummy_ai_idx != static_cast<int>(DummyAI::Idle)) {
            const auto& ds = scene.dummies();
            // 保证 strafe_dir 大小匹配
            if (strafe_dir.size() < ds.size()) strafe_dir.resize(ds.size(), 1);
            const DummyAI mode = static_cast<DummyAI>(dummy_ai_idx);
            for (std::size_t i = 0; i < ds.size(); ++i) {
                Unit* d = ds[i];
                if (!d || !d->alive()) continue;
                if (mode == DummyAI::Charge) {
                    // 用 OrderAttackTarget 让 dummy 走到 attack_range 内开打 --
                    // 比纯 issue_move 更接近 Dota A 键行为 (路径上对手就揍).
                    if (scene.caster() && scene.caster()->alive()) {
                        // 已经在攻击同一目标 -> 不重复入队 (覆盖会清掉 attack_cd
                        // 推进的 dispatched 状态, 但 OrderAttackTarget 是持续行为,
                        // 重复 issue 会反复清队, 实际无副作用; 这里通过 current_order
                        // 跳过避免噪音).
                        const auto* cur = d->current_order();
                        bool already = false;
                        if (cur) {
                            if (auto* at = std::get_if<OrderAttackTarget>(cur)) {
                                already = (at->target == scene.caster()->id());
                            }
                        }
                        if (!already) {
                            d->issue_order(OrderAttackTarget{scene.caster()->id()});
                        }
                    }
                } else if (mode == DummyAI::Strafe) {
                    // 在 (pos.x, pos.y ± 200) 之间往返. 到达后(无 target)切方向.
                    if (!d->move_target().has_value()) {
                        const Vec2 p = d->position();
                        d->issue_move({p.x, p.y + 200.0 * strafe_dir[i]});
                        strafe_dir[i] = -strafe_dir[i];
                    }
                }
            }
        }

        if (dt > 0.0) scene.update(dt);

        BeginDrawing();
        ClearBackground(Color{18, 22, 28, 255});

        // --- 战场区域裁剪 + 网格 + 单位 + 投射物 + 飘字 ---
        BeginScissorMode(field_x0, field_y0, cam.window_w, cam.window_h);
        dota::visual::draw_grid(cam, -1200, 1200, -800, 800, 200);
        for (const auto& p : scene.render_projectiles()) dota::visual::draw_projectile(cam, p);
        for (const auto& u : scene.render_units())       dota::visual::draw_unit(cam, u);
        if (Unit* selected = scene.find_unit(selected_unit_id)) {
            const Vector2 ss = cam.to_screen(selected->position());
            const float sr = std::max(dota::visual::kMinUnitRadiusPx,
                                      cam.scalar(selected->hull_radius()));
            DrawCircleLines(static_cast<int>(ss.x), static_cast<int>(ss.y),
                            sr + 5.0f, Color{120, 210, 255, 255});
            DrawCircleLines(static_cast<int>(ss.x), static_cast<int>(ss.y),
                            sr + 7.0f, Color{120, 210, 255, 180});
        }
        for (auto& f : scene.texts())
            dota::visual::draw_floating_text(cam, f, scene.world()->time());

        // --- 瞄准预览 ---
        if (aim != AimMode::None && selected_ability >= 0 &&
            selected_ability < slot_count && scene.caster() && scene.caster()->alive()) {
            Ability* ab = scene.caster_abilities()[selected_ability];
            const Vec2 caster_pos = scene.caster()->position();
            const double range = ab->cast_range();
            const Vector2 cs = cam.to_screen(caster_pos);

            // cast_range 圆 (浅色)
            if (range > 0.0) {
                DrawCircleLines(static_cast<int>(cs.x), static_cast<int>(cs.y),
                                cam.scalar(range), Color{200, 200, 80, 90});
            }

            if (aim == AimMode::AwaitUnitTarget) {
                if (hover_unit) {
                    const Vector2 us = cam.to_screen(hover_unit->position());
                    const double d = std::sqrt(dist2(caster_pos, hover_unit->position()));
                    const double effective_range = range + hover_unit->hull_radius();
                    const Color ring = (range <= 0.0 || d <= effective_range)
                        ? Color{255, 220, 80, 255}
                        : Color{220, 80, 80, 255};
                    const float r_px = std::max(dota::visual::kMinUnitRadiusPx,
                                                cam.scalar(hover_unit->hull_radius()));
                    DrawCircleLines(static_cast<int>(us.x), static_cast<int>(us.y),
                                    r_px + 4.0f, ring);
                    DrawCircleLines(static_cast<int>(us.x), static_cast<int>(us.y),
                                    r_px + 6.0f, ring);
                }
            } else if (aim == AimMode::AwaitPointTarget && mouse_in_field) {
                const Vector2 ts = cam.to_screen(mouse_world);
                const double d = std::sqrt(dist2(caster_pos, mouse_world));
                const Color line_c = (range <= 0.0 || d <= range)
                    ? Color{255, 220, 80, 200}
                    : Color{220, 80, 80, 200};

                LinearPreview lp;
                if (preview_linear(*ab, lp)) {
                    // 线性投射物: 画从 caster 沿瞄准方向, 长度 lp.length, 宽 lp.width
                    // 的胶囊 (= 中线 + 两端半圆 + 两侧平行线). 单位被命中条件:
                    // 圆心到该胶囊距离 <= hull_radius (由引擎处理).
                    const double dx = mouse_world.x - caster_pos.x;
                    const double dy = mouse_world.y - caster_pos.y;
                    const double len = std::sqrt(dx*dx + dy*dy);
                    if (len > 1e-3) {
                        const double ux = dx / len, uy = dy / len;
                        const Vec2 end_w{caster_pos.x + ux * lp.length,
                                         caster_pos.y + uy * lp.length};
                        const Vector2 es = cam.to_screen(end_w);
                        const float hw = cam.scalar(lp.width * 0.5);
                        // 法向单位向量 (-uy, ux), 转屏幕像素 (zoom 已在 hw 中)
                        const float nx = static_cast<float>(-uy) * hw;
                        const float ny = static_cast<float>( ux) * hw;
                        DrawLineEx({cs.x + nx, cs.y + ny},
                                   {es.x + nx, es.y + ny}, 1.5f, line_c);
                        DrawLineEx({cs.x - nx, cs.y - ny},
                                   {es.x - nx, es.y - ny}, 1.5f, line_c);
                        DrawCircleLines(static_cast<int>(cs.x), static_cast<int>(cs.y), hw, line_c);
                        DrawCircleLines(static_cast<int>(es.x), static_cast<int>(es.y), hw, line_c);
                        DrawLineEx(cs, es, 1.0f, Color{line_c.r, line_c.g, line_c.b, 120});
                    }
                } else {
                    // AoE 圆形预览
                    DrawLineEx(cs, ts, 2.0f, line_c);
                    const double size = preview_aoe_radius(*ab, 100.0);
                    DrawCircleLines(static_cast<int>(ts.x), static_cast<int>(ts.y),
                                    cam.scalar(size), line_c);
                    DrawLineEx({ts.x - 8, ts.y}, {ts.x + 8, ts.y}, 2.0f, line_c);
                    DrawLineEx({ts.x, ts.y - 8}, {ts.x, ts.y + 8}, 2.0f, line_c);
                }
            }
            // AwaitConfirmNoTarget: 预览只是 cast_range 圆, 已经画过.
        }

        // 移动目标 marker
        if (scene.caster()) {
            auto t = scene.caster()->move_target();
            if (t) {
                const Vector2 ms_pt = cam.to_screen(*t);
                const Color c{120, 230, 120, 200};
                DrawCircleLines(static_cast<int>(ms_pt.x),
                                static_cast<int>(ms_pt.y), 8.0f, c);
                DrawLineEx({ms_pt.x - 6, ms_pt.y}, {ms_pt.x + 6, ms_pt.y}, 1.5f, c);
                DrawLineEx({ms_pt.x, ms_pt.y - 6}, {ms_pt.x, ms_pt.y + 6}, 1.5f, c);
            }
        }
        EndScissorMode();

        // --- HUD 顶部状态行 ---
        DrawText(TextFormat("hero: %s   t = %.2fs%s",
                            catalog.heroes()[scene.hero_index()].yaml_name.c_str(),
                            scene.world()->time(),
                            paused ? "  [PAUSED]" : ""),
                 field_x0 + 12, 10, 20, RAYWHITE);

        // --- imgui 面板: Heroes (左) / Ability Bar (底) / Dummy Tuning (右) ---
        // 三块都钉死位置 + 关掉 move/resize/collapse, 当成固定 dock.
        constexpr ImGuiWindowFlags kFixedFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse;

        // Heroes 面板.
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kSidePanelW),
                                        static_cast<float>(kWindowH - kAbilityBarH)));
        if (ImGui::Begin("Heroes", nullptr, kFixedFlags)) {
            const int prev_active = hero_active;
            for (std::size_t i = 0; i < catalog.heroes().size(); ++i) {
                const bool sel = (static_cast<int>(i) == hero_active);
                if (ImGui::Selectable(catalog.heroes()[i].yaml_name.c_str(), sel)) {
                    hero_active = static_cast<int>(i);
                }
            }
            if (hero_active >= 0 && hero_active != prev_active &&
                static_cast<std::size_t>(hero_active) != scene.hero_index()) {
                scene.rebuild_with_hero(static_cast<std::size_t>(hero_active));
                select_caster();
                selected_ability = -1;
                paused = false;
            }
        }
        ImGui::End();

        // Ability Bar 面板 (底部, 横跨战场宽度).
        const int bar_y = kWindowH - kAbilityBarH;
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kSidePanelW),
                                        static_cast<float>(bar_y)));
        ImGui::SetNextWindowSize(ImVec2(
            static_cast<float>(kWindowW - kSidePanelW - kTunePanelW),
            static_cast<float>(kAbilityBarH)));
        if (ImGui::Begin("Abilities", nullptr,
                         kFixedFlags | ImGuiWindowFlags_NoTitleBar)) {
            const int slots = std::min<int>(
                kAbilitySlotMax,
                static_cast<int>(scene.caster_abilities().size()));
            if (slots == 0) {
                ImGui::TextDisabled("(no active abilities)");
            }
            const float slot_gap = ImGui::GetStyle().ItemSpacing.x;
            const float slot_w = slots > 0
                ? (ImGui::GetContentRegionAvail().x - slot_gap * (slots - 1)) / slots
                : 200.0f;
            const ImVec2 slot_sz(std::max(120.0f, slot_w), 64.0f);
            for (int i = 0; i < slots; ++i) {
                if (i > 0) ImGui::SameLine();
                Ability* ab = scene.caster_abilities()[i];
                const bool selected = (selected_ability == i);
                ImGui::PushID(i);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.95f, 0.78f, 0.25f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(1.0f, 0.85f, 0.35f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.85f, 0.7f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,0,0,1));
                }
                const double cd = ab->cooldown_remaining();
                const double mp = ab->mana_cost_for_level();
                const char* tag = behavior_label(ab->behavior());
                char label[128];
                std::snprintf(label, sizeof(label),
                              "[%d] %s\n%s  CD %.1fs  MP %d",
                              i + 1, ab->name().c_str(), tag, cd,
                              static_cast<int>(mp));
                if (ImGui::Button(label, slot_sz)) selected_ability = i;
                if (selected) ImGui::PopStyleColor(4);
                ImGui::PopID();
            }
        }
        ImGui::End();

        // Inspector 面板 (右侧).
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kWindowW - kTunePanelW), 0.0f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kTunePanelW),
                                        static_cast<float>(kWindowH)));
        if (ImGui::Begin("Inspector", nullptr, kFixedFlags)) {
            Unit* selected = scene.find_unit(selected_unit_id);
            if (!selected && scene.caster()) {
                select_caster();
                selected = scene.caster();
            }

            if (ImGui::BeginTabBar("##inspector_tabs")) {
                if (ImGui::BeginTabItem("Unit")) {
                    if (!selected) {
                        ImGui::TextDisabled("(no unit selected)");
                    } else {
                        ImGui::TextUnformatted(selected->name().c_str());
                        ImGui::Text("id %u  %s  %s",
                                    selected->id(), team_label(selected->team()),
                                    selected->alive() ? "alive" : "dead");
                        ImGui::Spacing();
                        if (ImGui::BeginTabBar("##unit_modules")) {
                            if (ImGui::BeginTabItem("Base")) {
                                const float half_w =
                                    (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
                                if (ImGui::Button("Caster", ImVec2(half_w, 26.0f))) {
                                    select_caster();
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Full", ImVec2(half_w, 26.0f))) {
                                    selected->set_health(selected->max_health());
                                    selected->set_mana(selected->max_mana());
                                }
                                if (ImGui::Button("Kill", ImVec2(half_w, 26.0f))) {
                                    selected->set_health(0.0);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Revive", ImVec2(half_w, 26.0f))) {
                                    selected->set_health(selected->max_health());
                                    selected->set_mana(selected->max_mana());
                                }

                                ImGui::SeparatorText("Vitals");
                                double hp = selected->health();
                                if (drag_double("HP", hp, 5.0f, 0.0,
                                                std::max(1.0, selected->max_health()), "%.0f")) {
                                    selected->set_health(hp);
                                }
                                double mana = selected->mana();
                                if (drag_double("Mana", mana, 5.0f, 0.0,
                                                std::max(1.0, selected->max_mana()), "%.0f")) {
                                    selected->set_mana(mana);
                                }

                                Vec2 pos = selected->position();
                                bool pos_changed = false;
                                pos_changed |= drag_double("Pos X", pos.x, 5.0f, -5000.0, 5000.0, "%.0f");
                                pos_changed |= drag_double("Pos Y", pos.y, 5.0f, -5000.0, 5000.0, "%.0f");
                                if (pos_changed) selected->set_position(pos);

                                ImGui::SeparatorText("Base Stats");
                                UnitStats stats = selected->stats();
                                bool stats_changed = false;
                                stats_changed |= drag_double("Max HP", stats.max_health,
                                                             10.0f, 1.0, 50000.0, "%.0f");
                                stats_changed |= drag_double("Max Mana", stats.max_mana,
                                                             10.0f, 0.0, 10000.0, "%.0f");
                                stats_changed |= drag_double("Attack", stats.attack_damage,
                                                             1.0f, 0.0, 1000.0, "%.0f");
                                stats_changed |= drag_double("Armor", stats.base_armor,
                                                             0.2f, -50.0, 200.0, "%.1f");
                                stats_changed |= drag_double("MR", stats.magic_resist,
                                                             0.01f, 0.0, 1.0, "%.2f");
                                stats_changed |= drag_double("Move Speed", stats.move_speed,
                                                             5.0f, 0.0, 2000.0, "%.0f");
                                stats_changed |= drag_double("Attack Range", stats.attack_range,
                                                             5.0f, 0.0, 2000.0, "%.0f");
                                stats_changed |= drag_double("Hull", stats.hull_radius,
                                                             1.0f, 0.0, 200.0, "%.0f");
                                if (stats_changed) selected->set_stats(stats);

                                ImGui::SeparatorText("Effective");
                                ImGui::Text("HP %.0f / %.0f", selected->health(), selected->max_health());
                                ImGui::Text("Mana %.0f / %.0f", selected->mana(), selected->max_mana());
                                ImGui::Text("Attack %.1f", selected->attack_damage());
                                ImGui::Text("Armor %.1f", selected->armor());
                                ImGui::Text("MR %.2f", selected->magic_resist());
                                ImGui::Text("Move Speed %.0f", selected->move_speed());
                                ImGui::Text("Regen %.1f / %.1f",
                                            selected->health_regen(), selected->mana_regen());
                                ImGui::Text("Amp %.2f  Status Res %.2f",
                                            selected->spell_amp_pct(), selected->status_resist());
                                ImGui::Text("Cast Range +%.0f", selected->cast_range_bonus());
                                ImGui::TextUnformatted("States");
                                draw_state_mask(selected->modifiers().aggregated_states());
                                ImGui::EndTabItem();
                            }

                            if (ImGui::BeginTabItem("Modifiers")) {
                                const auto& mods = selected->modifiers().all();
                                std::size_t remove_index = mods.size();
                                if (ImGui::BeginTable(
                                        "##mod_table", 4,
                                        ImGuiTableFlags_BordersInnerV |
                                        ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_Resizable)) {
                                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                                    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 74.0f);
                                    ImGui::TableSetupColumn("Stacks", ImGuiTableColumnFlags_WidthFixed, 58.0f);
                                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 54.0f);
                                    ImGui::TableHeadersRow();
                                    for (std::size_t i = 0; i < mods.size(); ++i) {
                                        Modifier* mod = mods[i].get();
                                        if (!mod) continue;
                                        ImGui::TableNextRow();
                                        ImGui::PushID(static_cast<int>(i));

                                        ImGui::TableSetColumnIndex(0);
                                        if (ImGui::TreeNode("##mod_details", "%s", mod->name().c_str())) {
                                            ImGui::Text("Purgable %s  Dispellable %s  %s",
                                                        mod->is_purgable() ? "yes" : "no",
                                                        mod->is_dispellable() ? "yes" : "no",
                                                        mod->is_debuff() ? "debuff" : "buff");
                                            ImGui::TextUnformatted("States");
                                            draw_state_mask(mod->declared_states());
                                            const auto props = mod->declared_properties();
                                            ImGui::TextUnformatted("Properties");
                                            if (props.empty()) {
                                                ImGui::TextDisabled("(none)");
                                            } else {
                                                for (const auto& p : props) {
                                                    ImGui::BulletText("%s %+0.2f",
                                                                      property_label(p.property), p.value);
                                                }
                                            }
                                            ImGui::TreePop();
                                        }

                                        ImGui::TableSetColumnIndex(1);
                                        double duration = mod->permanent()
                                            ? -1.0 : mod->duration_remaining();
                                        ImGui::SetNextItemWidth(-FLT_MIN);
                                        if (drag_double("##duration", duration,
                                                        0.05f, -1.0, 600.0, "%.1f")) {
                                            mod->refresh(duration < 0.0 ? -1.0 : duration);
                                        }

                                        ImGui::TableSetColumnIndex(2);
                                        int stacks = mod->stack_count();
                                        ImGui::SetNextItemWidth(-FLT_MIN);
                                        if (ImGui::DragInt("##stacks", &stacks, 0.1f, 1, 999)) {
                                            mod->set_stack_count(std::max(1, stacks));
                                        }

                                        ImGui::TableSetColumnIndex(3);
                                        if (ImGui::SmallButton("Del")) remove_index = i;
                                        ImGui::PopID();
                                    }
                                    ImGui::EndTable();
                                }
                                if (remove_index < mods.size()) {
                                    selected->modifiers().remove_at(remove_index);
                                }

                                ImGui::SeparatorText("Add");
                                static int add_mod_idx = 0;
                                static int last_add_mod_idx = -1;
                                static ModifierParamBag add_mod_params;
                                const auto modifier_catalog = build_modifier_catalog(scene);
                                if (add_mod_idx >= static_cast<int>(modifier_catalog.size())) {
                                    add_mod_idx = 0;
                                    last_add_mod_idx = -1;
                                }

                                if (modifier_catalog.empty()) {
                                    ImGui::TextDisabled("(no registered modifiers)");
                                } else {
                                    if (ImGui::BeginCombo(
                                            "Name", modifier_catalog[add_mod_idx].label.c_str())) {
                                        for (std::size_t i = 0; i < modifier_catalog.size(); ++i) {
                                            const bool is_selected =
                                                add_mod_idx == static_cast<int>(i);
                                            if (ImGui::Selectable(
                                                    modifier_catalog[i].label.c_str(), is_selected)) {
                                                add_mod_idx = static_cast<int>(i);
                                            }
                                            if (is_selected) ImGui::SetItemDefaultFocus();
                                        }
                                        ImGui::EndCombo();
                                    }

                                    const ModifierAddSpec& spec = modifier_catalog[add_mod_idx];
                                    if (last_add_mod_idx != add_mod_idx || add_mod_params.empty()) {
                                        reset_modifier_param_values(spec, add_mod_params);
                                        last_add_mod_idx = add_mod_idx;
                                    }
                                    draw_modifier_param_controls(spec, add_mod_params);

                                    if (ImGui::Button("Add Modifier", ImVec2(-FLT_MIN, 28.0f))) {
                                        std::unique_ptr<Modifier> mod =
                                            spec.create(*selected, add_mod_params);
                                        if (mod) {
                                            selected->modifiers().attach(std::move(mod));
                                            show_toast("Modifier added", Color{120, 230, 120, 255});
                                        } else {
                                            show_toast("Modifier unavailable",
                                                       Color{220, 100, 100, 255});
                                        }
                                    }
                                }
                                ImGui::EndTabItem();
                            }

                            if (ImGui::BeginTabItem("Ability")) {
                                const auto& abilities = selected->abilities().all();
                                std::size_t remove_ability_index = abilities.size();
                                if (abilities.empty()) {
                                    ImGui::TextDisabled("(no abilities)");
                                }
                                for (std::size_t i = 0; i < abilities.size(); ++i) {
                                    Ability* ab = abilities[i].get();
                                    if (!ab) continue;
                                    ImGui::PushID(static_cast<int>(i));
                                    if (ImGui::TreeNode("##ability_details", "%s", ab->name().c_str())) {
                                        ImGui::Text("Behavior %s  Phase %s",
                                                    behavior_label(ab->behavior()),
                                                    phase_label(ab->phase()));
                                        ImGui::Text("CD remaining %.1f", ab->cooldown_remaining());

                                        int level = ab->level();
                                        if (ImGui::DragInt("Level", &level, 0.1f, 1, 30)) {
                                            ab->set_level(level);
                                        }
                                        double cast_range = ab->cast_range();
                                        if (drag_double("Cast Range", cast_range,
                                                        5.0f, 0.0, 5000.0, "%.0f")) {
                                            ab->set_cast_range(cast_range);
                                        }
                                        double cast_point = ab->cast_point();
                                        if (drag_double("Cast Point", cast_point,
                                                        0.01f, 0.0, 10.0, "%.2f")) {
                                            ab->set_cast_point(cast_point);
                                        }
                                        double backswing = ab->backswing();
                                        if (drag_double("Backswing", backswing,
                                                        0.01f, 0.0, 10.0, "%.2f")) {
                                            ab->set_backswing(backswing);
                                        }
                                        double channel_time = ab->channel_time();
                                        if (drag_double("Channel Time", channel_time,
                                                        0.01f, 0.0, 30.0, "%.2f")) {
                                            ab->set_channel_time(channel_time);
                                        }
                                        double cooldown = ab->cooldown_for_level();
                                        if (drag_double("Cooldown", cooldown,
                                                        0.1f, 0.0, 300.0, "%.1f")) {
                                            ab->set_cooldown_levels({cooldown});
                                        }
                                        double mana_cost = ab->mana_cost_for_level();
                                        if (drag_double("Mana Cost", mana_cost,
                                                        1.0f, 0.0, 2000.0, "%.0f")) {
                                            ab->set_mana_cost_levels({mana_cost});
                                        }

                                        AbilitySpecial special = ab->ability_special();
                                        bool special_changed = false;
                                        if (!special.empty() && ImGui::TreeNode("Specials")) {
                                            for (auto& [key, value] : special) {
                                                const std::size_t value_count = value.is_int
                                                    ? value.ints.size() : value.floats.size();
                                                if (value_count == 0) continue;
                                                const std::size_t idx = static_cast<std::size_t>(
                                                    std::clamp(ab->level() - 1, 0,
                                                               static_cast<int>(value_count) - 1));
                                                double v = value.get_float(ab->level());
                                                if (drag_double(key.c_str(), v,
                                                                value.is_int ? 1.0f : 0.05f,
                                                                -10000.0, 10000.0,
                                                                value.is_int ? "%.0f" : "%.2f")) {
                                                    if (value.is_int) {
                                                        const long iv = static_cast<long>(std::llround(v));
                                                        value.ints[idx] = iv;
                                                        if (idx < value.floats.size()) {
                                                            value.floats[idx] = static_cast<double>(iv);
                                                        }
                                                    } else {
                                                        value.floats[idx] = v;
                                                    }
                                                    special_changed = true;
                                                }
                                            }
                                            ImGui::TreePop();
                                        }
                                        if (special_changed) {
                                            ab->set_ability_special(std::move(special));
                                        }

                                        if (ImGui::Button("Remove Ability", ImVec2(-FLT_MIN, 26.0f))) {
                                            remove_ability_index = i;
                                        }
                                        ImGui::TreePop();
                                    }
                                    ImGui::PopID();
                                }
                                if (remove_ability_index < abilities.size()) {
                                    scene.remove_ability_at(*selected, remove_ability_index);
                                    selected_ability = -1;
                                    show_toast("Ability removed", Color{200, 200, 80, 255});
                                }

                                ImGui::SeparatorText("Add");
                                const auto& choices = scene.ability_choices();
                                static int add_ability_idx = 0;
                                if (add_ability_idx >= static_cast<int>(choices.size())) {
                                    add_ability_idx = 0;
                                }
                                if (choices.empty()) {
                                    ImGui::TextDisabled("(no registered abilities)");
                                } else {
                                    if (ImGui::BeginCombo("Name", choices[add_ability_idx].label.c_str())) {
                                        for (std::size_t i = 0; i < choices.size(); ++i) {
                                            const bool selected_item =
                                                add_ability_idx == static_cast<int>(i);
                                            if (ImGui::Selectable(choices[i].label.c_str(), selected_item)) {
                                                add_ability_idx = static_cast<int>(i);
                                            }
                                            if (selected_item) ImGui::SetItemDefaultFocus();
                                        }
                                        ImGui::EndCombo();
                                    }
                                    if (ImGui::Button("Add Ability", ImVec2(-FLT_MIN, 28.0f))) {
                                        Ability* added = scene.add_ability_to(
                                            *selected, choices[add_ability_idx].name);
                                        if (added) {
                                            selected_ability = -1;
                                            show_toast("Ability added", Color{120, 230, 120, 255});
                                        } else {
                                            show_toast("Ability add failed", Color{220, 100, 100, 255});
                                        }
                                    }
                                }
                                ImGui::EndTabItem();
                            }

                            ImGui::EndTabBar();
                        }
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Scenario")) {
                    ImGui::SeparatorText("Dummy Stats");
                    ImGui::SliderFloat("HP",   &tune_max_health,  100.0f, 10000.0f, "%.0f");
                    ImGui::SliderFloat("MR+",  &tune_mr_bonus,     -1.0f,    1.0f,  "%+.2f");
                    ImGui::SliderFloat("Arm+", &tune_armor_bonus, -10.0f,   30.0f,  "%+.1f");
                    ImGui::SliderFloat("AD",   &tune_attack_dmg,    0.0f,  200.0f,  "%.0f");
                    ImGui::Spacing();
                    const float btn_w = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
                    if (ImGui::Button("Apply", ImVec2(btn_w, 28.0f))) {
                        apply_dummy_tune(true);
                        show_toast("Dummy stats applied", Color{120, 230, 120, 255});
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset", ImVec2(btn_w, 28.0f))) {
                        tune_max_health  = 6000.0f;
                        tune_attack_dmg  = 0.0f;
                        tune_mr_bonus    = 0.0f;
                        tune_armor_bonus = 0.0f;
                        scene.set_dummy_override({});
                        scene.rebuild_with_hero(scene.hero_index());
                        select_caster();
                        selected_ability = -1;
                        aim = AimMode::None;
                        show_toast("Dummies reset", Color{200, 200, 80, 255});
                    }

                    ImGui::SeparatorText("Dummy AI");
                    const char* ai_items[] = {"Idle", "Strafe", "Charge"};
                    const int prev_ai = dummy_ai_idx;
                    ImGui::Combo("Mode", &dummy_ai_idx, ai_items, IM_ARRAYSIZE(ai_items));
                    if (prev_ai != dummy_ai_idx) {
                        // 切到 Idle 时清掉所有 dummy 的 move 指令
                        if (dummy_ai_idx == static_cast<int>(DummyAI::Idle)) {
                            for (Unit* d : scene.dummies()) {
                                if (d) d->stop_move();
                            }
                        }
                    }
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();

        // --- 帮助文字 (技能栏上方一行) ---
        const char* aim_hint = "";
        switch (aim) {
            case AimMode::AwaitUnitTarget:    aim_hint = "  [aim: click a target]"; break;
            case AimMode::AwaitPointTarget:   aim_hint = "  [aim: click a point]"; break;
            case AimMode::AwaitConfirmNoTarget: aim_hint = "  [confirm: SPACE / number / left-click]"; break;
            default: break;
        }
        DrawText(TextFormat(
                     "1-4 / click to select   LMB cast   RMB move / cancel   "
                     "S stop   ESC cancel   R reset   SPACE pause%s",
                     aim_hint),
                 kSidePanelW + 12, kWindowH - kAbilityBarH - 22,
                 14, Color{160, 160, 160, 255});

        // --- Toast (战场顶部居中) ---
        const double age = scene.world()->time() - toast_t0;
        if (age >= 0.0 && age < 1.5) {
            const float alpha = static_cast<float>(1.0 - age / 1.5);
            Color tc = toast_color;
            tc.a = static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
            const int tw = MeasureText(toast_text.c_str(), 22);
            const int tx = field_x0 + (cam.window_w - tw) / 2;
            const int ty = 38;
            DrawText(toast_text.c_str(), tx, ty, 22, tc);
        }

        rlImGuiEnd();
        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
