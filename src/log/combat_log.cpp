#include "dota/log/combat_log.hpp"

#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"

#include <cstdio>
#include <utility>

namespace dota {

namespace {

// 解析单位名. World 没找到时返回空串.
std::string lookup_name(World& w, EntityId id) {
    if (id == kInvalidEntityId) return {};
    Unit* u = w.find(id);
    return u ? u->name() : std::string{};
}

const char* dtype_label(DamageType t) {
    switch (t) {
        case DamageType::Physical: return "physical";
        case DamageType::Magical:  return "magical";
        case DamageType::Pure:     return "pure";
    }
    return "?";
}

// 名字回退: 优先用 snapshot 的 name, 没有再用 id 的字符串.
std::string display(const std::string& name, EntityId id) {
    if (!name.empty()) return name;
    if (id == kInvalidEntityId) return "?";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%u", id);
    return buf;
}

} // namespace

CombatLog::CombatLog(World& world, std::size_t capacity)
    : world_(world), capacity_(capacity) {
    auto& bus = world_.events();

    tokens_.push_back(bus.subscribe<DamageAppliedEvent>(
        [this](DamageAppliedEvent& e) {
            CombatLogEntry entry;
            entry.kind        = CombatLogKind::Damage;
            entry.source      = e.attacker;
            entry.target      = e.victim;
            entry.source_name = lookup_name(world_, e.attacker);
            entry.target_name = lookup_name(world_, e.victim);
            entry.dtype       = e.type;
            entry.amount      = e.amount_applied;
            entry.amount_pre  = e.amount_pre;
            push(std::move(entry));
        }));

    tokens_.push_back(bus.subscribe<HealAppliedEvent>(
        [this](HealAppliedEvent& e) {
            CombatLogEntry entry;
            entry.kind        = CombatLogKind::Heal;
            entry.source      = e.healer;
            entry.target      = e.target;
            entry.source_name = lookup_name(world_, e.healer);
            entry.target_name = lookup_name(world_, e.target);
            entry.amount      = e.amount;
            push(std::move(entry));
        }));

    tokens_.push_back(bus.subscribe<ModifierAddedEvent>(
        [this](ModifierAddedEvent& e) {
            CombatLogEntry entry;
            entry.kind        = CombatLogKind::ModifierAdded;
            entry.target      = e.unit;
            entry.target_name = lookup_name(world_, e.unit);
            entry.name        = e.name;
            entry.amount      = e.duration;
            entry.flag        = (e.duration < 0.0);  // permanent
            entry.stacks      = e.stacks;
            push(std::move(entry));
        }));

    tokens_.push_back(bus.subscribe<ModifierRemovedEvent>(
        [this](ModifierRemovedEvent& e) {
            CombatLogEntry entry;
            entry.kind        = CombatLogKind::ModifierRemoved;
            entry.target      = e.unit;
            entry.target_name = lookup_name(world_, e.unit);
            entry.name        = e.name;
            push(std::move(entry));
        }));

    tokens_.push_back(bus.subscribe<AbilityCastStartedEvent>(
        [this](AbilityCastStartedEvent& e) {
            CombatLogEntry entry;
            entry.kind        = CombatLogKind::AbilityCastStarted;
            entry.source      = e.caster;
            entry.target      = e.target_unit;
            entry.source_name = lookup_name(world_, e.caster);
            entry.target_name = lookup_name(world_, e.target_unit);
            entry.name        = e.ability;
            push(std::move(entry));
        }));

    tokens_.push_back(bus.subscribe<AbilityCastFinishedEvent>(
        [this](AbilityCastFinishedEvent& e) {
            CombatLogEntry entry;
            entry.kind        = CombatLogKind::AbilityCastFinished;
            entry.source      = e.caster;
            entry.source_name = lookup_name(world_, e.caster);
            entry.name        = e.ability;
            entry.flag        = e.interrupted;
            push(std::move(entry));
        }));

    tokens_.push_back(bus.subscribe<AttackLandedEvent>(
        [this](AttackLandedEvent& e) {
            CombatLogEntry entry;
            entry.kind        = CombatLogKind::AttackLanded;
            entry.source      = e.attacker;
            entry.target      = e.victim;
            entry.source_name = lookup_name(world_, e.attacker);
            entry.target_name = lookup_name(world_, e.victim);
            entry.amount      = e.damage;
            entry.flag        = e.missed;
            entry.dtype       = DamageType::Physical;
            push(std::move(entry));
        }));

    tokens_.push_back(bus.subscribe<UnitDiedEvent>(
        [this](UnitDiedEvent& e) {
            CombatLogEntry entry;
            entry.kind        = CombatLogKind::UnitDied;
            entry.source      = e.killer;
            entry.target      = e.victim;
            entry.source_name = lookup_name(world_, e.killer);
            entry.target_name = lookup_name(world_, e.victim);
            push(std::move(entry));
        }));
}

CombatLog::~CombatLog() {
    // World 通常先析构, 此时 events() 已经销毁; 这里能调到 unsubscribe 仅是
    // World 还活着的少数情况(比如显式 reset CombatLog). 双向都安全.
    auto& bus = world_.events();
    for (auto t : tokens_) {
        bus.unsubscribe<DamageAppliedEvent>(t);
        bus.unsubscribe<HealAppliedEvent>(t);
        bus.unsubscribe<ModifierAddedEvent>(t);
        bus.unsubscribe<ModifierRemovedEvent>(t);
        bus.unsubscribe<AbilityCastStartedEvent>(t);
        bus.unsubscribe<AbilityCastFinishedEvent>(t);
        bus.unsubscribe<AttackLandedEvent>(t);
        bus.unsubscribe<UnitDiedEvent>(t);
    }
}

void CombatLog::push(CombatLogEntry e) {
    e.time = world_.time();
    // 此处取不到 World 的 tick 序号(私有), 用 time / kTickDt 反算近似值即可,
    // 误差 ≤ 1 tick, 仅供 UI 排序展示.
    e.tick = static_cast<std::uint64_t>(e.time / World::kTickDt + 0.5);
    if (filter_ && !filter_(e)) return;
    if (capacity_ > 0 && ring_.size() >= capacity_) {
        ring_.pop_front();
    }
    ring_.push_back(std::move(e));
}

std::string CombatLog::format(const CombatLogEntry& e) {
    char buf[256];
    const std::string s = display(e.source_name, e.source);
    const std::string t = display(e.target_name, e.target);
    switch (e.kind) {
        case CombatLogKind::Damage:
            std::snprintf(buf, sizeof(buf),
                "[%6.2f] %s hits %s for %.0f %s damage (pre %.0f)",
                e.time, s.c_str(), t.c_str(), e.amount,
                dtype_label(e.dtype), e.amount_pre);
            break;
        case CombatLogKind::Heal:
            std::snprintf(buf, sizeof(buf),
                "[%6.2f] %s heals %s for %.0f",
                e.time, s.c_str(), t.c_str(), e.amount);
            break;
        case CombatLogKind::ModifierAdded:
            if (e.flag) {
                std::snprintf(buf, sizeof(buf),
                    "[%6.2f] %s gains %s (permanent, %d stack%s)",
                    e.time, t.c_str(), e.name.c_str(), e.stacks,
                    e.stacks == 1 ? "" : "s");
            } else {
                std::snprintf(buf, sizeof(buf),
                    "[%6.2f] %s gains %s (%.1fs, %d stack%s)",
                    e.time, t.c_str(), e.name.c_str(), e.amount, e.stacks,
                    e.stacks == 1 ? "" : "s");
            }
            break;
        case CombatLogKind::ModifierRemoved:
            std::snprintf(buf, sizeof(buf),
                "[%6.2f] %s loses %s",
                e.time, t.c_str(), e.name.c_str());
            break;
        case CombatLogKind::AbilityCastStarted:
            if (e.target != kInvalidEntityId) {
                std::snprintf(buf, sizeof(buf),
                    "[%6.2f] %s casts %s on %s",
                    e.time, s.c_str(), e.name.c_str(), t.c_str());
            } else {
                std::snprintf(buf, sizeof(buf),
                    "[%6.2f] %s casts %s",
                    e.time, s.c_str(), e.name.c_str());
            }
            break;
        case CombatLogKind::AbilityCastFinished:
            std::snprintf(buf, sizeof(buf),
                "[%6.2f] %s %s %s",
                e.time, s.c_str(),
                e.flag ? "interrupted" : "finished",
                e.name.c_str());
            break;
        case CombatLogKind::AttackLanded:
            if (e.flag) {
                std::snprintf(buf, sizeof(buf),
                    "[%6.2f] %s attacks %s -- missed",
                    e.time, s.c_str(), t.c_str());
            } else {
                std::snprintf(buf, sizeof(buf),
                    "[%6.2f] %s attacks %s for %.0f",
                    e.time, s.c_str(), t.c_str(), e.amount);
            }
            break;
        case CombatLogKind::UnitDied:
            if (e.source != kInvalidEntityId) {
                std::snprintf(buf, sizeof(buf),
                    "[%6.2f] %s died (killer: %s)",
                    e.time, t.c_str(), s.c_str());
            } else {
                std::snprintf(buf, sizeof(buf),
                    "[%6.2f] %s died",
                    e.time, t.c_str());
            }
            break;
    }
    return buf;
}

} // namespace dota
