// raylib 实时可视化 demo. 加载 Pudge / Lina / Sven 三个英雄打一场剧本化的小战斗,
// 屏幕上显示英雄圆 + HP 条 + 修饰器图标 + 施法读条 + 投射物 + 伤害飘字.
//
// 控制: SPACE 暂停 / 继续, R 重置场景, ESC 退出.

#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/projectile/manager.hpp"
#include "dota/projectile/projectile.hpp"
#include "dota/script/lua_state.hpp"

#include "raylib.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

// 飘字: 伤害 / 治疗事件触发
struct FloatingText {
    Vec2        world_pos;
    std::string text;
    Color       color;
    double      spawn_time;   // 用于自动 fade
};

// 剧本动作, 在指定 (相对于场景开始的) 时间触发.
struct ScriptedCast {
    double           t;
    Ability*         ability;
    CastTarget       target;
    bool             fired{false};
};

// 把世界坐标变换到屏幕像素. 视图固定居中, 缩放固定 -- 6 英雄场景在 ±800 范围内可放下.
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

struct Scenario {
    LuaState                          lua;
    AbilityRegistry                   reg;
    std::unique_ptr<World>            world;
    Unit*                             pudge{};
    Unit*                             lina{};
    Unit*                             sven{};
    std::vector<ScriptedCast>         schedule;
    std::vector<FloatingText>         texts;
    double                            local_time{0.0};

    Scenario() {
        reg.set_lua(&lua);
        const std::string base = data_dir();
        reg.load_file(base + "/heroes/pudge.yaml");
        reg.load_file(base + "/heroes/lina.yaml");
        reg.load_file(base + "/heroes/sven.yaml");

        world = std::make_unique<World>();

        UnitStats hero;
        hero.max_health    = 1500.0;
        hero.max_mana      = 700.0;
        hero.attack_damage = 55.0;
        hero.base_armor    = 2.0;
        hero.magic_resist  = 0.25;
        hero.base_attack_time = 1.7;

        pudge = world->spawn("Pudge", Team::Radiant, hero, {-800.0, -100.0});
        sven  = world->spawn("Sven",  Team::Radiant, hero, {-800.0,  150.0});
        lina  = world->spawn("Lina",  Team::Dire,    hero, { 600.0,    0.0});

        // 订阅伤害 / 治疗事件 → 飘字
        world->events().subscribe<DamageAppliedEvent>(
            [this](DamageAppliedEvent& e) {
                if (e.amount_applied <= 0.0) return;
                Unit* v = world->find(e.victim);
                if (!v) return;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "-%.0f", e.amount_applied);
                Color c = (e.type == DamageType::Magical) ? Color{120, 180, 255, 255}
                        : (e.type == DamageType::Pure)    ? Color{255, 220, 120, 255}
                                                          : Color{255, 100, 100, 255};
                texts.push_back({v->position(), buf, c, world->time()});
            });
        world->events().subscribe<HealAppliedEvent>(
            [this](HealAppliedEvent& e) {
                if (e.amount <= 0.0) return;
                Unit* v = world->find(e.target);
                if (!v) return;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "+%.0f", e.amount);
                texts.push_back({v->position(), buf, Color{120, 230, 120, 255},
                                 world->time()});
            });
        world->events().subscribe<UnitDiedEvent>(
            [this](UnitDiedEvent& e) {
                Unit* v = world->find(e.victim);
                if (!v) return;
                texts.push_back({v->position(), "DEAD",
                                 Color{255, 60, 60, 255}, world->time()});
            });

        // --- 剧本: 在固定时间点触发施法 ---
        auto add = [&](double t, const std::string& ability_name, Unit& caster,
                       Unit* unit_target, Vec2 point) {
            Ability* a = reg.instantiate(ability_name, caster);
            if (!a) return;
            CastTarget tgt;
            if (unit_target) { tgt.unit = unit_target; }
            else             { tgt.point = point; tgt.has_point = true; }
            schedule.push_back({t, a, tgt, false});
        };

        // Pudge 肉钩 lina, 拉过来.
        add(0.5,  "pudge_meat_hook",  *pudge, nullptr, lina->position());
        // Sven 风暴之锤 lina (跟踪投射物).
        add(2.5,  "sven_storm_hammer", *sven,  lina,    {});
        // Pudge 拉过来后用肢解 (引导, 自我治疗).
        add(3.8,  "pudge_dismember",   *pudge, lina,    {});
        // Lina 醒过来反打 dragon slave (穿透 linear projectile).
        add(7.0,  "lina_dragon_slave", *lina,  nullptr, pudge->position());
        // Lina 收尾 light strike array (点 AOE + delay).
        add(8.0,  "lina_light_strike_array", *lina, nullptr, pudge->position());
    }

    void update(double dt) {
        // 触发到点的脚本
        for (auto& s : schedule) {
            if (s.fired) continue;
            if (local_time + dt < s.t) continue;
            // 攻击者必须存活才施法
            // (由于我们不是直接拿 caster, 通过 ability->caster() 拿到)
            // 用 Ability 接口: 大部分情况下 caster.alive() 会被 can_cast 检查, 但 cast point
            // 静默失败也无所谓, 视觉上看不到就算了.
            if (s.target.unit && !s.target.unit->alive()) {
                s.fired = true;
                continue;
            }
            s.ability->order_cast(s.target, *world);
            s.fired = true;
        }

        world->advance(dt);
        local_time += dt;

        // 清理过期飘字 (1.2s 寿命)
        const double now = world->time();
        texts.erase(
            std::remove_if(texts.begin(), texts.end(),
                           [now](const FloatingText& f) {
                               return now - f.spawn_time > 1.2;
                           }),
            texts.end());
    }

    bool finished() const {
        if (local_time < 12.0) return false;
        for (auto& s : schedule) if (!s.fired) return false;
        return true;
    }
};

// --- 渲染 ---

void draw_unit(const ViewCamera& cam, const Unit& u) {
    const Vector2 c = cam.to_screen(u.position());
    const Color body = team_color(u.team(), u.alive());

    // 圆形单位
    DrawCircleV(c, kUnitRadiusPx, body);
    DrawCircleLines(static_cast<int>(c.x), static_cast<int>(c.y),
                    kUnitRadiusPx, BLACK);

    // HP 条
    const float bar_w = 60.0f;
    const float bar_h = 6.0f;
    const float bar_y = c.y - kUnitRadiusPx - 14.0f;
    const float bar_x = c.x - bar_w * 0.5f;
    const float frac  = u.alive()
        ? static_cast<float>(u.health() / u.max_health())
        : 0.0f;
    DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                  static_cast<int>(bar_w), static_cast<int>(bar_h),
                  Color{40, 40, 40, 230});
    DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                  static_cast<int>(bar_w * std::clamp(frac, 0.0f, 1.0f)),
                  static_cast<int>(bar_h),
                  Color{80, 220, 80, 255});

    // 名字
    const int name_w = MeasureText(u.name().c_str(), 14);
    DrawText(u.name().c_str(),
             static_cast<int>(c.x) - name_w / 2,
             static_cast<int>(bar_y) - 16, 14, RAYWHITE);

    // 修饰器图标行: 单位下方, 每个 10x10 方块
    const auto& mods = u.modifiers().all();
    const float mod_y = c.y + kUnitRadiusPx + 6.0f;
    float mod_x = c.x - (mods.size() * 12.0f) * 0.5f;
    for (auto& m : mods) {
        Color mc = m->is_debuff() ? Color{220, 80, 80, 255}
                                  : Color{80, 180, 220, 255};
        DrawRectangle(static_cast<int>(mod_x), static_cast<int>(mod_y),
                      10, 10, mc);
        DrawRectangleLines(static_cast<int>(mod_x), static_cast<int>(mod_y),
                           10, 10, BLACK);
        mod_x += 12.0f;
    }

    // 施法读条: 找正在 Casting 的 ability
    for (const auto& a : u.abilities().all()) {
        if (a->phase() != CastPhase::Casting) continue;
        const float total = static_cast<float>(a->cast_point());
        if (total <= 0.0f) continue;
        const float remaining = static_cast<float>(a->phase_timer());
        const float p = 1.0f - std::clamp(remaining / total, 0.0f, 1.0f);
        const float cb_w = 70.0f;
        const float cb_h = 5.0f;
        const float cb_y = c.y + kUnitRadiusPx + 22.0f;
        const float cb_x = c.x - cb_w * 0.5f;
        DrawRectangle(static_cast<int>(cb_x), static_cast<int>(cb_y),
                      static_cast<int>(cb_w), static_cast<int>(cb_h),
                      Color{20, 20, 20, 220});
        DrawRectangle(static_cast<int>(cb_x), static_cast<int>(cb_y),
                      static_cast<int>(cb_w * p), static_cast<int>(cb_h),
                      Color{255, 220, 80, 255});
        break;
    }
}

void draw_projectile(const ViewCamera& cam, const Projectile& p) {
    const Vector2 c = cam.to_screen(p.position());
    if (p.is_linear()) {
        // 用一条短小的指示线显示朝向 + width
        const Vec2 dir = p.direction();
        const double tail_world = 60.0; // 尾迹长度 (世界坐标)
        const Vec2 tail{p.position().x - dir.x * tail_world,
                        p.position().y - dir.y * tail_world};
        const Vector2 t = cam.to_screen(tail);
        const float thickness = std::max(2.0f, cam.scalar(p.width() * 0.3));
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

int main() {
    InitWindow(kWindowW, kWindowH, "dota2_skill -- raylib visual demo");
    SetTargetFPS(60);

    auto scenario = std::make_unique<Scenario>();
    ViewCamera cam;
    bool paused = false;

    while (!WindowShouldClose()) {
        // --- 输入 ---
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_R)) {
            scenario = std::make_unique<Scenario>();
            paused = false;
        }

        // --- 推进 ---
        const float dt_raw = GetFrameTime();
        const double dt = paused ? 0.0 : std::min(static_cast<double>(dt_raw), 0.05);
        if (dt > 0.0) scenario->update(dt);

        // --- 绘制 ---
        BeginDrawing();
        ClearBackground(Color{18, 22, 28, 255});

        // 网格背景, 给空间感
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

        World& w = *scenario->world;

        // 投射物在单位下方画
        for (const auto& p : w.projectiles().live()) {
            if (p) draw_projectile(cam, *p);
        }

        // 单位
        for (Unit* u : w.units_on_team(Team::Radiant)) draw_unit(cam, *u);
        for (Unit* u : w.units_on_team(Team::Dire))    draw_unit(cam, *u);

        // 飘字
        for (auto& f : scenario->texts) draw_floating_text(cam, f, w.time());

        // HUD
        DrawText(TextFormat("t = %.2fs%s", w.time(), paused ? " [PAUSED]" : ""),
                 12, 10, 20, RAYWHITE);
        DrawText("SPACE pause   R reset   ESC quit",
                 12, kWindowH - 26, 16, Color{160, 160, 160, 255});

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
