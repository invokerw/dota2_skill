#include "scene.hpp"

#include "dota/projectile/manager.hpp"
#include "dota/projectile/projectile.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace dota::skill_tester {

using tools::HeroEntry;

Scene::Scene(const tools::HeroCatalog& cat) : catalog_(cat) {
    build_ability_choices();
    rebuild_with_hero(0);
}

void Scene::rebuild_with_hero(std::size_t idx) {
    if (idx >= catalog_.heroes().size()) return;
    hero_index_ = idx;

    // 销毁顺序: World 持有的 ScriptedAbility 内部 sol::table / sol::function
    // 引用了 lua_ 拥有的 lua_State. 必须先销毁 world_ (连带 reg_), 再销毁
    // lua_, 否则 sol 析构时会对已销毁的 state 调 luaL_unref 触发 segfault.
    world_.reset();
    reg_.reset();
    lua_.reset();

    lua_ = std::make_unique<LuaState>();
    reg_ = std::make_unique<AbilityRegistry>();
    reg_->set_lua(lua_.get());
    // Inspector 允许给任意单位添加任意已注册技能, 因此每次重建都加载全部 hero YAML.
    for (const auto& hero : catalog_.heroes()) {
        reg_->load_file(hero.yaml_path);
    }

    world_ = std::make_unique<World>();
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

    caster_abilities_.clear();
    for (const auto& a : h.abilities) {
        if (a.is_passive) continue;
        if (Ability* inst = reg_->instantiate(a.name, *caster_)) {
            caster_abilities_.push_back(inst);
        }
    }
    sync_caster_abilities();

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

    // 飘字订阅: 伤害 / 治疗事件 -> FloatingText.
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

void Scene::update(double dt) {
    if (!world_) return;
    world_->advance(dt);
    const double now = world_->time();
    texts_.erase(
        std::remove_if(texts_.begin(), texts_.end(),
            [now](const visual::FloatingText& f) {
                return now - f.spawn_time > 1.2;
            }),
        texts_.end());
}

Unit* Scene::find_unit(EntityId id) const {
    return world_ ? world_->find(id) : nullptr;
}

std::vector<Unit*> Scene::units() const {
    std::vector<Unit*> out;
    if (!world_) return out;
    for (Team t : {Team::Radiant, Team::Dire, Team::Neutral}) {
        for (Unit* u : world_->units_on_team(t)) {
            if (u) out.push_back(u);
        }
    }
    return out;
}

void Scene::sync_caster_abilities() {
    caster_abilities_.clear();
    if (!caster_) return;
    for (const auto& a : caster_->abilities().all()) {
        if (a && !a->is_passive()) caster_abilities_.push_back(a.get());
    }
}

Ability* Scene::add_ability_to(Unit& unit, const std::string& name) {
    if (!reg_) return nullptr;
    Ability* ab = reg_->instantiate(name, unit);
    if (ab && &unit == caster_) sync_caster_abilities();
    return ab;
}

bool Scene::remove_ability_at(Unit& unit, std::size_t index) {
    const bool ok = unit.abilities().remove_at(index);
    if (ok && &unit == caster_) sync_caster_abilities();
    return ok;
}

std::vector<visual::RenderUnit> Scene::render_units() const {
    std::vector<visual::RenderUnit> out;
    if (!world_) return out;
    for (Team t : {Team::Radiant, Team::Dire, Team::Neutral}) {
        for (Unit* u : world_->units_on_team(t)) {
            if (!u) continue;
            visual::RenderUnit ru;
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

std::vector<visual::RenderProjectile> Scene::render_projectiles() const {
    std::vector<visual::RenderProjectile> out;
    if (!world_) return out;
    for (const auto& p : world_->projectiles().live()) {
        if (!p) continue;
        visual::RenderProjectile rp;
        rp.pid    = p->pid();
        rp.pos    = p->position();
        rp.linear = p->is_linear();
        rp.dir    = p->direction();
        rp.width  = p->width();
        out.push_back(rp);
    }
    return out;
}

void Scene::build_ability_choices() {
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

} // namespace dota::skill_tester
