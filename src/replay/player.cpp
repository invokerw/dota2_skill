#include "dota/replay/player.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <sstream>
#include <string>

namespace dota::replay {

namespace {

// --- 极简 JSON 字段抽取. 录像格式字段固定, 不需要完整 JSON parser. ---
// 所有 helper 都按字符串扫描查找 `"key":` 之后的 token, 不处理嵌套数组里的同名 key
// (这里不会冲突, 因为 schema 里 key 都全局唯一).

bool find_key(const std::string& s, const std::string& key, std::size_t& pos) {
    const std::string needle = "\"" + key + "\":";
    pos = s.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    return pos < s.size();
}

bool extract_string(const std::string& s, const std::string& key, std::string& out) {
    std::size_t p;
    if (!find_key(s, key, p)) return false;
    if (s[p] != '"') return false;
    ++p;
    std::string r;
    while (p < s.size() && s[p] != '"') {
        if (s[p] == '\\' && p + 1 < s.size()) {
            char c = s[p + 1];
            switch (c) {
                case '"':  r += '"';  break;
                case '\\': r += '\\'; break;
                case 'n':  r += '\n'; break;
                case 'r':  r += '\r'; break;
                case 't':  r += '\t'; break;
                default:   r += c;    break;
            }
            p += 2;
        } else {
            r += s[p++];
        }
    }
    out = std::move(r);
    return true;
}

bool extract_double(const std::string& s, const std::string& key, double& out) {
    std::size_t p;
    if (!find_key(s, key, p)) return false;
    char* endp = nullptr;
    out = std::strtod(s.c_str() + p, &endp);
    return endp != s.c_str() + p;
}

bool extract_int(const std::string& s, const std::string& key, long long& out) {
    std::size_t p;
    if (!find_key(s, key, p)) return false;
    char* endp = nullptr;
    out = std::strtoll(s.c_str() + p, &endp, 10);
    return endp != s.c_str() + p;
}

bool extract_bool(const std::string& s, const std::string& key, bool& out) {
    std::size_t p;
    if (!find_key(s, key, p)) return false;
    if (s.compare(p, 4, "true") == 0)  { out = true;  return true; }
    if (s.compare(p, 5, "false") == 0) { out = false; return true; }
    return false;
}

// 抽取 "key":[a,b] 形式的 vec2.
bool extract_vec2(const std::string& s, const std::string& key, Vec2& out) {
    std::size_t p;
    if (!find_key(s, key, p)) return false;
    if (s[p] != '[') return false;
    ++p;
    char* endp = nullptr;
    out.x = std::strtod(s.c_str() + p, &endp);
    if (endp == s.c_str() + p) return false;
    p = static_cast<std::size_t>(endp - s.c_str());
    while (p < s.size() && (s[p] == ',' || std::isspace(static_cast<unsigned char>(s[p])))) ++p;
    out.y = std::strtod(s.c_str() + p, &endp);
    return endp != s.c_str() + p;
}

DamageType parse_dtype(const std::string& s) {
    if (s == "magical") return DamageType::Magical;
    if (s == "pure")    return DamageType::Pure;
    return DamageType::Physical;
}

} // namespace

Player::Player()  = default;
Player::~Player() = default;

bool Player::load_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    return load(in);
}

bool Player::load(std::istream& in) {
    frames_.clear();
    tick_rate_ = 30;
    scenario_.clear();

    std::string line;
    bool got_header = false;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (!got_header) {
            long long v = 0;
            if (!extract_int(line, "v", v) || v != 1) return false;
            long long tr = 30;
            extract_int(line, "tick_rate", tr);
            tick_rate_ = static_cast<int>(tr);
            extract_string(line, "scenario", scenario_);
            got_header = true;
            continue;
        }

        Frame f;
        double t = 0.0;
        long long tk = 0;
        extract_double(line, "t", t);
        extract_int(line, "tick", tk);
        f.t = t;
        f.tick = static_cast<std::uint64_t>(tk);

        // positions: "positions":[[id,x,y],[id,x,y],...]
        std::size_t pp = line.find("\"positions\":[");
        if (pp != std::string::npos) {
            pp += std::string("\"positions\":[").size();
            while (pp < line.size() && line[pp] != ']') {
                if (line[pp] != '[') { ++pp; continue; }
                ++pp;
                char* endp = nullptr;
                long long id = std::strtoll(line.c_str() + pp, &endp, 10);
                pp = static_cast<std::size_t>(endp - line.c_str());
                while (pp < line.size() && line[pp] == ',') ++pp;
                double x = std::strtod(line.c_str() + pp, &endp);
                pp = static_cast<std::size_t>(endp - line.c_str());
                while (pp < line.size() && line[pp] == ',') ++pp;
                double y = std::strtod(line.c_str() + pp, &endp);
                pp = static_cast<std::size_t>(endp - line.c_str());
                while (pp < line.size() && line[pp] != ']') ++pp;
                if (pp < line.size()) ++pp;  // 跳过 ']'
                f.positions.emplace_back(static_cast<EntityId>(id), x, y);
                while (pp < line.size() && line[pp] == ',') ++pp;
            }
        }

        // events: 拆分顶层 {...}, 不递归
        std::size_t ep = line.find("\"events\":[");
        if (ep != std::string::npos) {
            ep += std::string("\"events\":[").size();
            int depth = 0;
            std::size_t start = std::string::npos;
            while (ep < line.size()) {
                char c = line[ep];
                if (depth == 0 && c == ']') break;
                if (c == '{') {
                    if (depth == 0) start = ep;
                    ++depth;
                } else if (c == '}') {
                    --depth;
                    if (depth == 0 && start != std::string::npos) {
                        f.events.push_back(line.substr(start, ep - start + 1));
                        start = std::string::npos;
                    }
                }
                ++ep;
            }
        }

        frames_.push_back(std::move(f));
    }

    reset_state();
    return got_header;
}

double Player::duration() const {
    return frames_.empty() ? 0.0 : frames_.back().t;
}

void Player::reset_state() {
    units_.clear();
    projectiles_.clear();
    damage_buffer_.clear();
    heal_buffer_.clear();
    death_buffer_.clear();
    current_frame_ = 0;
    time_ = 0.0;
}

void Player::seek(double t) {
    reset_state();
    advance(t);
}

void Player::advance(double dt) {
    time_ += dt;
    while (current_frame_ < frames_.size() && frames_[current_frame_].t <= time_) {
        apply_frame(frames_[current_frame_]);
        ++current_frame_;
    }
    recompute_projectile_positions();
}

void Player::apply_frame(const Frame& f) {
    // 先处理事件 (顺序: spawn 等需要在 position 更新之前生效)
    for (const auto& e : f.events) apply_event(e, f.t);

    // 用 positions 全量刷新存活单位坐标
    for (const auto& [id, x, y] : f.positions) {
        auto it = units_.find(id);
        if (it != units_.end()) it->second.position = {x, y};
    }
}

void Player::apply_event(const std::string& json, double t) {
    std::string type;
    if (!extract_string(json, "type", type)) return;

    if (type == "unit_spawn") {
        UnitView u;
        long long id = 0; extract_int(json, "id", id);
        u.id = static_cast<EntityId>(id);
        extract_string(json, "name", u.name);
        long long team = 0; extract_int(json, "team", team);
        u.team = static_cast<Team>(team);
        extract_double(json, "max_hp", u.max_hp);
        extract_double(json, "max_mana", u.max_mana);
        u.hp = u.max_hp;
        extract_vec2(json, "pos", u.position);
        u.alive = true;
        units_[u.id] = std::move(u);
    } else if (type == "unit_died") {
        long long id = 0; extract_int(json, "id", id);
        long long killer = 0; extract_int(json, "killer", killer);
        auto it = units_.find(static_cast<EntityId>(id));
        if (it != units_.end()) {
            it->second.alive = false;
            it->second.hp = 0.0;
            it->second.casting_ability.clear();
        }
        death_buffer_.push_back({static_cast<EntityId>(id),
                                 static_cast<EntityId>(killer), t});
    } else if (type == "cast_start") {
        long long caster = 0; extract_int(json, "caster", caster);
        std::string ability;  extract_string(json, "ability", ability);
        auto it = units_.find(static_cast<EntityId>(caster));
        if (it != units_.end()) {
            it->second.casting_ability = ability;
            it->second.cast_started_t  = t;
        }
    } else if (type == "cast_finish") {
        long long caster = 0; extract_int(json, "caster", caster);
        auto it = units_.find(static_cast<EntityId>(caster));
        if (it != units_.end()) it->second.casting_ability.clear();
    } else if (type == "projectile_spawn") {
        ProjectileView p;
        long long pid = 0;    extract_int(json, "pid", pid);   p.pid = static_cast<EntityId>(pid);
        long long src = 0;    extract_int(json, "src", src);   p.source = static_cast<EntityId>(src);
        extract_vec2(json, "origin", p.origin);
        extract_double(json, "speed", p.speed);
        extract_bool(json, "tracking", p.tracking);
        if (!p.tracking) {
            extract_vec2(json, "dir", p.dir);
            extract_double(json, "length", p.length);
            extract_double(json, "width", p.width);
        } else {
            long long tgt = 0; extract_int(json, "target", tgt);
            p.target = static_cast<EntityId>(tgt);
        }
        p.spawn_t  = t;
        p.last_pos = p.origin;
        projectiles_[p.pid] = std::move(p);
    } else if (type == "projectile_finish") {
        long long pid = 0; extract_int(json, "pid", pid);
        projectiles_.erase(static_cast<EntityId>(pid));
    } else if (type == "modifier_add") {
        long long unit = 0; extract_int(json, "unit", unit);
        std::string name;   extract_string(json, "name", name);
        auto it = units_.find(static_cast<EntityId>(unit));
        if (it != units_.end()) it->second.modifiers.push_back(name);
    } else if (type == "modifier_remove") {
        long long unit = 0; extract_int(json, "unit", unit);
        std::string name;   extract_string(json, "name", name);
        auto it = units_.find(static_cast<EntityId>(unit));
        if (it != units_.end()) {
            auto& v = it->second.modifiers;
            auto eit = std::find(v.begin(), v.end(), name);
            if (eit != v.end()) v.erase(eit);
        }
    } else if (type == "damage") {
        long long src = 0; extract_int(json, "src", src);
        long long dst = 0; extract_int(json, "dst", dst);
        std::string dt; extract_string(json, "dtype", dt);
        double applied = 0.0; extract_double(json, "applied", applied);
        auto it = units_.find(static_cast<EntityId>(dst));
        if (it != units_.end()) it->second.hp = std::max(0.0, it->second.hp - applied);
        damage_buffer_.push_back({static_cast<EntityId>(src),
                                  static_cast<EntityId>(dst),
                                  parse_dtype(dt), applied, t});
    } else if (type == "heal") {
        long long src = 0; extract_int(json, "src", src);
        long long dst = 0; extract_int(json, "dst", dst);
        double amount = 0.0; extract_double(json, "amount", amount);
        auto it = units_.find(static_cast<EntityId>(dst));
        if (it != units_.end())
            it->second.hp = std::min(it->second.max_hp, it->second.hp + amount);
        heal_buffer_.push_back({static_cast<EntityId>(src),
                                static_cast<EntityId>(dst), amount, t});
    }
    // attack_landed / projectile_hit 仅作为状态通知, 我们已经从 damage 里拿到伤害,
    // 不需要单独处理.
}

void Player::recompute_projectile_positions() {
    for (auto& [pid, p] : projectiles_) {
        if (p.tracking) {
            // 锁定目标位置 (近似), 否则维持上次位置.
            auto it = units_.find(p.target);
            if (it != units_.end()) p.last_pos = it->second.position;
        } else {
            const double elapsed = time_ - p.spawn_t;
            const double travelled = std::min(p.length, p.speed * elapsed);
            p.last_pos = {p.origin.x + p.dir.x * travelled,
                          p.origin.y + p.dir.y * travelled};
        }
    }
}

} // namespace dota::replay
