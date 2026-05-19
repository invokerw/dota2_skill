// raylib 可视化 demo. 支持两种模式:
//
//   ./duel_visual                    -- live: 内嵌剧本场景, 实时跑引擎
//   ./duel_visual --replay file.jsonl -- replay: 直接读 Recorder 录像回放
//
// 控制: SPACE 暂停 / 继续, R 重置, ESC 退出.

#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/projectile/manager.hpp"
#include "dota/projectile/projectile.hpp"
#include "dota/replay/player.hpp"
#include "dota/script/lua_state.hpp"

#include "raylib.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace dota;

namespace {

constexpr int   kWindowW = 1280;
constexpr int   kWindowH = 720;
constexpr float kUnitRadiusPx = 26.0f;

std::string data_dir() {
    if (const char* d = std::getenv("DOTA_DATA_DIR")) return d;
    return DOTA_DATA_DIR;
}

// --- 渲染端中间表示 (live / replay 都填这个) ---

struct RenderUnit {
    EntityId    id;
    std::string name;
    Team        team;
    bool        alive;
    double      hp;
    double      max_hp;
    Vec2        position;
    std::vector<std::string> modifiers;  // 名字; 用于显示数量
    std::string casting_ability;          // 当前 cast 的技能, 空 = 没在 cast
    float       cast_progress{-1.0f};     // 0..1; <0 表示未知 (replay)
};

struct RenderProjectile {
    EntityId pid;
    Vec2     pos;
    Vec2     dir;
    double   width;
    bool     linear;
};

// 飘字 (live / replay 共用)
struct FloatingText {
    Vec2        world_pos;
    std::string text;
    Color       color;
    double      spawn_time;
};

struct ViewCamera {
    float zoom{0.45f};
    Vec2  center{0.0f, 0.0f};
    Vector2 to_screen(Vec2 w) const {
        return {
            kWindowW * 0.5f + static_cast<float>((w.x - center.x) * zoom),
            kWindowH * 0.5f + static_cast<float>((w.y - center.y) * zoom),
        };
    }
    float scalar(double s) const { return static_cast<float>(s * zoom); }
};

Color team_color(Team t, bool alive) {
    if (!alive) return Color{90, 90, 90, 255};
    switch (t) {
        case Team::Radiant: return Color{86, 171, 47, 255};
        case Team::Dire:    return Color{200, 60, 60, 255};
        default:            return Color{160, 160, 160, 255};
    }
}

// --- 数据源接口 ---
class IRenderSource {
public:
    virtual ~IRenderSource() = default;
    virtual void   update(double dt)              = 0;
    virtual void   reset()                        = 0;
    virtual double time()                   const = 0;
    virtual std::vector<RenderUnit>       units() const       = 0;
    virtual std::vector<RenderProjectile> projectiles() const = 0;
    virtual std::vector<FloatingText>&    texts()             = 0;
    virtual bool   finished()               const = 0;
    virtual const char* mode_label()        const = 0;
};

// --- Live 场景: 跑真实 World ---
struct ScriptedCast {
    double      t;
    Ability*    ability;
    CastTarget  target;
    bool        fired{false};
};

class LiveSource : public IRenderSource {
public:
    LiveSource() { build(); }

    void update(double dt) override {
        for (auto& s : schedule_) {
            if (s.fired) continue;
            if (local_time_ + dt < s.t) continue;
            if (s.target.unit && !s.target.unit->alive()) {
                s.fired = true;
                continue;
            }
            s.ability->order_cast(s.target, *world_);
            s.fired = true;
        }
        world_->advance(dt);
        local_time_ += dt;
        const double now = world_->time();
        texts_.erase(
            std::remove_if(texts_.begin(), texts_.end(),
                [now](const FloatingText& f) { return now - f.spawn_time > 1.2; }),
            texts_.end());
    }
    void reset() override { build(); }
    double time() const override { return world_->time(); }
    bool   finished() const override {
        if (local_time_ < 12.0) return false;
        for (auto& s : schedule_) if (!s.fired) return false;
        return true;
    }
    const char* mode_label() const override { return "live"; }

    std::vector<RenderUnit> units() const override {
        std::vector<RenderUnit> out;
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
    std::vector<RenderProjectile> projectiles() const override {
        std::vector<RenderProjectile> out;
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
    std::vector<FloatingText>& texts() override { return texts_; }

private:
    void build() {
        lua_     = std::make_unique<LuaState>();
        reg_     = std::make_unique<AbilityRegistry>();
        reg_->set_lua(lua_.get());
        const std::string base = data_dir();
        reg_->load_file(base + "/heroes/pudge.yaml");
        reg_->load_file(base + "/heroes/lina.yaml");
        reg_->load_file(base + "/heroes/sven.yaml");

        world_ = std::make_unique<World>();
        UnitStats hero;
        hero.max_health    = 1500.0;
        hero.max_mana      = 700.0;
        hero.attack_damage = 55.0;
        hero.base_armor    = 2.0;
        hero.magic_resist  = 0.25;
        hero.base_attack_time = 1.7;

        pudge_ = world_->spawn("Pudge", Team::Radiant, hero, {-800.0, -100.0});
        sven_  = world_->spawn("Sven",  Team::Radiant, hero, {-800.0,  150.0});
        lina_  = world_->spawn("Lina",  Team::Dire,    hero, { 600.0,    0.0});

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
        world_->events().subscribe<UnitDiedEvent>(
            [this](UnitDiedEvent& e) {
                Unit* v = world_->find(e.victim);
                if (!v) return;
                texts_.push_back({v->position(), "DEAD",
                                 Color{255, 60, 60, 255}, world_->time()});
            });

        schedule_.clear();
        local_time_ = 0.0;
        auto add = [&](double t, const std::string& name, Unit& caster,
                       Unit* unit_target, Vec2 point) {
            Ability* a = reg_->instantiate(name, caster);
            if (!a) return;
            CastTarget tgt;
            if (unit_target) tgt.unit = unit_target;
            else { tgt.point = point; tgt.has_point = true; }
            schedule_.push_back({t, a, tgt, false});
        };
        add(0.5, "pudge_meat_hook",        *pudge_, nullptr, lina_->position());
        add(2.5, "sven_storm_hammer",      *sven_,  lina_,    {});
        add(3.8, "pudge_dismember",        *pudge_, lina_,    {});
        add(7.0, "lina_dragon_slave",      *lina_,  nullptr, pudge_->position());
        add(8.0, "lina_light_strike_array",*lina_,  nullptr, pudge_->position());
    }

    std::unique_ptr<LuaState>        lua_;
    std::unique_ptr<AbilityRegistry> reg_;
    std::unique_ptr<World>           world_;
    Unit*                            pudge_{};
    Unit*                            sven_{};
    Unit*                            lina_{};
    std::vector<ScriptedCast>        schedule_;
    std::vector<FloatingText>        texts_;
    double                           local_time_{0.0};
};

// --- Replay 场景: 读 JSONL 文件 ---
class ReplaySource : public IRenderSource {
public:
    explicit ReplaySource(std::string path) : path_(std::move(path)) {
        if (!load()) loaded_ = false;
    }
    bool ok() const { return loaded_; }

    void update(double dt) override {
        if (!loaded_) return;
        const std::size_t before_d = player_.damages().size();
        const std::size_t before_h = player_.heals().size();
        const std::size_t before_x = player_.deaths().size();
        player_.advance(dt);
        // 把 Player 收到的伤害 / 治疗 / 死亡转成飘字
        const auto& dmgs = player_.damages();
        for (std::size_t i = before_d; i < dmgs.size(); ++i) {
            const auto& d = dmgs[i];
            auto it = player_.units().find(d.dst);
            if (it == player_.units().end()) continue;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "-%.0f", d.applied);
            Color c = (d.type == DamageType::Magical) ? Color{120, 180, 255, 255}
                    : (d.type == DamageType::Pure)    ? Color{255, 220, 120, 255}
                                                      : Color{255, 100, 100, 255};
            texts_.push_back({it->second.position, buf, c, player_.time()});
        }
        const auto& hls = player_.heals();
        for (std::size_t i = before_h; i < hls.size(); ++i) {
            const auto& h = hls[i];
            auto it = player_.units().find(h.dst);
            if (it == player_.units().end()) continue;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "+%.0f", h.amount);
            texts_.push_back({it->second.position, buf, Color{120, 230, 120, 255},
                              player_.time()});
        }
        const auto& dts = player_.deaths();
        for (std::size_t i = before_x; i < dts.size(); ++i) {
            const auto& dr = dts[i];
            auto it = player_.units().find(dr.id);
            if (it == player_.units().end()) continue;
            texts_.push_back({it->second.position, "DEAD",
                              Color{255, 60, 60, 255}, player_.time()});
        }
        const double now = player_.time();
        texts_.erase(
            std::remove_if(texts_.begin(), texts_.end(),
                [now](const FloatingText& f) { return now - f.spawn_time > 1.2; }),
            texts_.end());
    }
    void reset() override { load(); texts_.clear(); }
    double time() const override { return player_.time(); }
    bool   finished() const override { return player_.finished(); }
    const char* mode_label() const override { return "replay"; }

    std::vector<RenderUnit> units() const override {
        std::vector<RenderUnit> out;
        for (const auto& [id, u] : player_.units()) {
            RenderUnit ru;
            ru.id              = u.id;
            ru.name            = u.name;
            ru.team            = u.team;
            ru.alive           = u.alive;
            ru.hp              = u.hp;
            ru.max_hp          = u.max_hp;
            ru.position        = u.position;
            ru.modifiers       = u.modifiers;
            ru.casting_ability = u.casting_ability;
            ru.cast_progress   = -1.0f;  // replay 不知道 cast_point
            out.push_back(std::move(ru));
        }
        return out;
    }
    std::vector<RenderProjectile> projectiles() const override {
        std::vector<RenderProjectile> out;
        for (const auto& [pid, p] : player_.projectiles()) {
            RenderProjectile rp;
            rp.pid    = p.pid;
            rp.pos    = p.last_pos;
            rp.linear = !p.tracking;
            rp.dir    = p.dir;
            rp.width  = p.width;
            out.push_back(rp);
        }
        return out;
    }
    std::vector<FloatingText>& texts() override { return texts_; }

private:
    bool load() {
        // Player::load 内部会清空旧状态, 直接复用同一对象即可.
        loaded_ = player_.load_file(path_);
        return loaded_;
    }

    std::string                path_;
    replay::Player             player_;
    std::vector<FloatingText>  texts_;
    bool                       loaded_{false};
};

// --- 渲染 ---

void draw_unit(const ViewCamera& cam, const RenderUnit& u) {
    const Vector2 c = cam.to_screen(u.position);
    const Color body = team_color(u.team, u.alive);
    DrawCircleV(c, kUnitRadiusPx, body);
    DrawCircleLines(static_cast<int>(c.x), static_cast<int>(c.y),
                    kUnitRadiusPx, BLACK);

    const float bar_w = 60.0f;
    const float bar_h = 6.0f;
    const float bar_y = c.y - kUnitRadiusPx - 14.0f;
    const float bar_x = c.x - bar_w * 0.5f;
    const float frac  = u.alive ? static_cast<float>(u.hp / u.max_hp) : 0.0f;
    DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                  static_cast<int>(bar_w), static_cast<int>(bar_h),
                  Color{40, 40, 40, 230});
    DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                  static_cast<int>(bar_w * std::clamp(frac, 0.0f, 1.0f)),
                  static_cast<int>(bar_h), Color{80, 220, 80, 255});

    const int name_w = MeasureText(u.name.c_str(), 14);
    DrawText(u.name.c_str(),
             static_cast<int>(c.x) - name_w / 2,
             static_cast<int>(bar_y) - 16, 14, RAYWHITE);

    const float mod_y = c.y + kUnitRadiusPx + 6.0f;
    float mod_x = c.x - (u.modifiers.size() * 12.0f) * 0.5f;
    for (auto& name : u.modifiers) {
        // replay 没有 is_debuff 信息, 用名字里 _slow / _stun / _hex / _silence / _dot 粗判
        const bool dbg =
            name.find("_slow")  != std::string::npos ||
            name.find("_stun")  != std::string::npos ||
            name.find("_hex")   != std::string::npos ||
            name.find("_silence") != std::string::npos ||
            name.find("_drag")  != std::string::npos ||
            name.find("_dot")   != std::string::npos ||
            name.find("burning")!= std::string::npos;
        Color mc = dbg ? Color{220, 80, 80, 255} : Color{80, 180, 220, 255};
        DrawRectangle(static_cast<int>(mod_x), static_cast<int>(mod_y),
                      10, 10, mc);
        DrawRectangleLines(static_cast<int>(mod_x), static_cast<int>(mod_y),
                           10, 10, BLACK);
        mod_x += 12.0f;
    }

    if (!u.casting_ability.empty()) {
        const float cb_w = 70.0f;
        const float cb_h = 5.0f;
        const float cb_y = c.y + kUnitRadiusPx + 22.0f;
        const float cb_x = c.x - cb_w * 0.5f;
        DrawRectangle(static_cast<int>(cb_x), static_cast<int>(cb_y),
                      static_cast<int>(cb_w), static_cast<int>(cb_h),
                      Color{20, 20, 20, 220});
        const float p = u.cast_progress >= 0.0f ? u.cast_progress : 0.5f;
        DrawRectangle(static_cast<int>(cb_x), static_cast<int>(cb_y),
                      static_cast<int>(cb_w * p), static_cast<int>(cb_h),
                      Color{255, 220, 80, 255});
    }
}

void draw_projectile(const ViewCamera& cam, const RenderProjectile& p) {
    const Vector2 c = cam.to_screen(p.pos);
    if (p.linear) {
        const double tail_world = 60.0;
        const Vec2 tail{p.pos.x - p.dir.x * tail_world,
                        p.pos.y - p.dir.y * tail_world};
        const Vector2 t = cam.to_screen(tail);
        const float thickness = std::max(2.0f, cam.scalar(p.width * 0.3));
        DrawLineEx(t, c, thickness, Color{255, 200, 60, 220});
        DrawCircleV(c, std::max(4.0f, thickness * 0.7f),
                    Color{255, 240, 120, 255});
    } else {
        DrawCircleV(c, 8.0f, Color{255, 160, 60, 255});
        DrawCircleLines(static_cast<int>(c.x), static_cast<int>(c.y),
                        8.0f, BLACK);
    }
}

void draw_floating_text(const ViewCamera& cam, const FloatingText& f, double now) {
    const double age = now - f.spawn_time;
    const float life = 1.2f;
    const float t = static_cast<float>(age / life);
    const Vector2 base = cam.to_screen(f.world_pos);
    const float dy = -50.0f * t;
    const unsigned char alpha = static_cast<unsigned char>(
        std::max(0.0f, 255.0f * (1.0f - t)));
    Color c = f.color; c.a = alpha;
    const int w = MeasureText(f.text.c_str(), 18);
    DrawText(f.text.c_str(),
             static_cast<int>(base.x) - w / 2,
             static_cast<int>(base.y) - kUnitRadiusPx - 30 + static_cast<int>(dy),
             18, c);
}

} // namespace

int main(int argc, char** argv) {
    std::string replay_path;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            replay_path = argv[++i];
        }
    }

    InitWindow(kWindowW, kWindowH, "dota2_skill -- raylib visual demo");
    SetTargetFPS(60);

    std::unique_ptr<IRenderSource> source;
    if (!replay_path.empty()) {
        auto rs = std::make_unique<ReplaySource>(replay_path);
        if (!rs->ok()) {
            std::fprintf(stderr, "无法加载录像: %s\n", replay_path.c_str());
            CloseWindow();
            return 1;
        }
        source = std::move(rs);
    } else {
        source = std::make_unique<LiveSource>();
    }

    ViewCamera cam;
    bool paused = false;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_R))     { source->reset(); paused = false; }

        const float dt_raw = GetFrameTime();
        const double dt = paused ? 0.0 : std::min(static_cast<double>(dt_raw), 0.05);
        if (dt > 0.0) source->update(dt);

        BeginDrawing();
        ClearBackground(Color{18, 22, 28, 255});

        for (int gx = -1200; gx <= 1200; gx += 200) {
            const Vector2 a = cam.to_screen({static_cast<double>(gx), -800.0});
            const Vector2 b = cam.to_screen({static_cast<double>(gx),  800.0});
            DrawLineV(a, b, Color{32, 38, 46, 255});
        }
        for (int gy = -800; gy <= 800; gy += 200) {
            const Vector2 a = cam.to_screen({-1200.0, static_cast<double>(gy)});
            const Vector2 b = cam.to_screen({ 1200.0, static_cast<double>(gy)});
            DrawLineV(a, b, Color{32, 38, 46, 255});
        }

        for (const auto& p : source->projectiles()) draw_projectile(cam, p);
        for (const auto& u : source->units())       draw_unit(cam, u);
        for (auto& f : source->texts())             draw_floating_text(cam, f, source->time());

        DrawText(TextFormat("[%s] t = %.2fs%s",
                            source->mode_label(), source->time(),
                            paused ? " [PAUSED]" : ""),
                 12, 10, 20, RAYWHITE);
        DrawText("SPACE pause   R reset   ESC quit",
                 12, kWindowH - 26, 16, Color{160, 160, 160, 255});

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
