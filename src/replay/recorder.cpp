#include "dota/replay/recorder.hpp"

#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/modifier.hpp"

#include <cstdio>
#include <ostream>
#include <sstream>
#include <string>

namespace dota {

namespace {

// --- 简易 JSON 编码 helper (字段固定, 不需要完整 JSON 库) ---

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string fmt_double(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

std::string vec2_str(Vec2 v) {
    return "[" + fmt_double(v.x) + "," + fmt_double(v.y) + "]";
}

const char* dtype_str(DamageType t) {
    switch (t) {
        case DamageType::Physical: return "physical";
        case DamageType::Magical:  return "magical";
        case DamageType::Pure:     return "pure";
    }
    return "physical";
}

} // namespace

Recorder::Recorder(World& world, std::ostream& out)
    : world_(world), out_(out) {

    auto& bus = world_.events();

    bus.subscribe<UnitSpawnedEvent>([this](UnitSpawnedEvent& e){
        Unit* u = world_.find(e.id);
        if (!u) return;
        std::ostringstream s;
        s << "{\"type\":\"unit_spawn\",\"id\":" << e.id
          << ",\"name\":\"" << json_escape(u->name()) << "\""
          << ",\"team\":" << static_cast<int>(u->team())
          << ",\"max_hp\":" << fmt_double(u->max_health())
          << ",\"max_mana\":" << fmt_double(u->max_mana())
          << ",\"pos\":" << vec2_str(u->position())
          << "}";
        tick_events_.push_back(s.str());
    });

    bus.subscribe<UnitDiedEvent>([this](UnitDiedEvent& e){
        std::ostringstream s;
        s << "{\"type\":\"unit_died\",\"id\":" << e.victim
          << ",\"killer\":" << e.killer << "}";
        tick_events_.push_back(s.str());
    });

    bus.subscribe<AbilityCastStartedEvent>([this](AbilityCastStartedEvent& e){
        std::ostringstream s;
        s << "{\"type\":\"cast_start\",\"caster\":" << e.caster
          << ",\"ability\":\"" << json_escape(e.ability) << "\"";
        if (e.has_point) s << ",\"point\":" << vec2_str(e.target_point);
        if (e.target_unit != kInvalidEntityId) s << ",\"target\":" << e.target_unit;
        s << "}";
        tick_events_.push_back(s.str());
    });

    bus.subscribe<AbilityCastFinishedEvent>([this](AbilityCastFinishedEvent& e){
        std::ostringstream s;
        s << "{\"type\":\"cast_finish\",\"caster\":" << e.caster
          << ",\"ability\":\"" << json_escape(e.ability) << "\""
          << ",\"interrupted\":" << (e.interrupted ? "true" : "false") << "}";
        tick_events_.push_back(s.str());
    });

    bus.subscribe<ProjectileSpawnedEvent>([this](ProjectileSpawnedEvent& e){
        std::ostringstream s;
        s << "{\"type\":\"projectile_spawn\",\"pid\":" << e.pid
          << ",\"src\":" << e.source
          << ",\"origin\":" << vec2_str(e.origin)
          << ",\"speed\":" << fmt_double(e.speed)
          << ",\"tracking\":" << (e.linear ? "false" : "true");
        if (e.linear) {
            s << ",\"dir\":" << vec2_str(e.dir)
              << ",\"length\":" << fmt_double(e.length)
              << ",\"width\":" << fmt_double(e.width);
        } else if (e.target != kInvalidEntityId) {
            s << ",\"target\":" << e.target;
        }
        if (!e.name.empty()) {
            s << ",\"name\":\"" << json_escape(e.name) << "\"";
        }
        s << "}";
        tick_events_.push_back(s.str());
    });

    bus.subscribe<ProjectileHitEvent>([this](ProjectileHitEvent& e){
        std::ostringstream s;
        s << "{\"type\":\"projectile_hit\",\"pid\":" << e.pid
          << ",\"victim\":" << e.victim
          << ",\"point\":" << vec2_str(e.point) << "}";
        tick_events_.push_back(s.str());
    });

    bus.subscribe<ProjectileFinishedEvent>([this](ProjectileFinishedEvent& e){
        std::ostringstream s;
        s << "{\"type\":\"projectile_finish\",\"pid\":" << e.pid << "}";
        tick_events_.push_back(s.str());
    });

    bus.subscribe<ModifierAddedEvent>([this](ModifierAddedEvent& e){
        std::ostringstream s;
        s << "{\"type\":\"modifier_add\",\"unit\":" << e.unit
          << ",\"name\":\"" << json_escape(e.name) << "\""
          << ",\"duration\":" << fmt_double(e.duration)
          << ",\"stacks\":" << e.stacks << "}";
        tick_events_.push_back(s.str());
    });

    bus.subscribe<ModifierRemovedEvent>([this](ModifierRemovedEvent& e){
        std::ostringstream s;
        s << "{\"type\":\"modifier_remove\",\"unit\":" << e.unit
          << ",\"name\":\"" << json_escape(e.name) << "\"}";
        tick_events_.push_back(s.str());
    });

    bus.subscribe<DamageAppliedEvent>([this](DamageAppliedEvent& e){
        std::ostringstream s;
        s << "{\"type\":\"damage\",\"src\":" << e.attacker
          << ",\"dst\":" << e.victim
          << ",\"dtype\":\"" << dtype_str(e.type) << "\""
          << ",\"pre\":" << fmt_double(e.amount_pre)
          << ",\"applied\":" << fmt_double(e.amount_applied)
          << ",\"flags\":" << e.flags << "}";
        tick_events_.push_back(s.str());
    });

    bus.subscribe<HealAppliedEvent>([this](HealAppliedEvent& e){
        std::ostringstream s;
        s << "{\"type\":\"heal\",\"src\":" << e.healer
          << ",\"dst\":" << e.target
          << ",\"amount\":" << fmt_double(e.amount) << "}";
        tick_events_.push_back(s.str());
    });

    bus.subscribe<AttackLandedEvent>([this](AttackLandedEvent& e){
        std::ostringstream s;
        s << "{\"type\":\"attack_landed\",\"src\":" << e.attacker
          << ",\"dst\":" << e.victim
          << ",\"dmg\":" << fmt_double(e.damage)
          << ",\"missed\":" << (e.missed ? "true" : "false")
          << ",\"record\":" << e.record_id << "}";
        tick_events_.push_back(s.str());
    });

    bus.subscribe<TickEndEvent>([this](TickEndEvent& e){
        flush_frame_line(e.time, e.tick);
    });
}

Recorder::~Recorder() = default;

void Recorder::write_header(const std::string& scenario_label) {
    out_ << "{\"v\":1,\"tick_rate\":" << static_cast<int>(World::kTickRate)
         << ",\"scenario\":\"" << json_escape(scenario_label) << "\"}\n";
}

void Recorder::flush_frame_line(double t, std::uint64_t tick) {
    out_ << "{\"t\":" << fmt_double(t)
         << ",\"tick\":" << tick
         << ",\"positions\":[";
    bool first = true;
    // 注: World 没有公开"所有单位"接口, 但有 units_on_team. Neutral / Radiant / Dire
    // 三个都查一次即可覆盖. 用 cast 拿到 mutable 引用以使用 units_on_team.
    World& w = world_;
    for (Team t_team : {Team::Neutral, Team::Radiant, Team::Dire}) {
        for (Unit* u : w.units_on_team(t_team)) {
            if (!u) continue;
            if (!first) out_ << ",";
            first = false;
            out_ << "[" << u->id() << "," << fmt_double(u->position().x)
                 << "," << fmt_double(u->position().y) << "]";
        }
    }
    out_ << "],\"events\":[";
    for (std::size_t i = 0; i < tick_events_.size(); ++i) {
        if (i > 0) out_ << ",";
        out_ << tick_events_[i];
    }
    out_ << "]}\n";

    tick_events_.clear();
    ++frames_written_;
}

} // namespace dota
