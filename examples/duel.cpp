#include "dota/ability/registry.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/script/lua_state.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

// 第六阶段演示：Lion vs Juggernaut vs Lina — 三英雄团战
// 展示完整的技能 + 修改器 + 伤害管线。每个英雄
// 在自动攻击的同时依次施放其标志性技能。

namespace {

std::string data_dir() {
    if (const char* d = std::getenv("DOTA_DATA_DIR")) return d;
    return DOTA_DATA_DIR;
}

void log_header(const char* msg) {
    std::printf("\n=== %s ===\n", msg);
}

} // namespace

int main() {
    using namespace dota;

    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);

    const std::string base = data_dir();
    reg.load_file(base + "/heroes/lion.yaml");
    reg.load_file(base + "/heroes/juggernaut.yaml");
    reg.load_file(base + "/heroes/lina.yaml");

    World world;

    // --- 生成英雄 ---
    UnitStats lion_stats;
    lion_stats.max_health       = 800.0;
    lion_stats.max_mana         = 600.0;
    lion_stats.attack_damage    = 52.0;
    lion_stats.base_armor       = 1.0;
    lion_stats.magic_resist     = 0.25;
    lion_stats.base_attack_time = 1.7;
    lion_stats.attack_speed     = 100.0;

    UnitStats jug_stats;
    jug_stats.max_health       = 900.0;
    jug_stats.max_mana         = 400.0;
    jug_stats.attack_damage    = 60.0;
    jug_stats.base_armor       = 4.0;
    jug_stats.magic_resist     = 0.25;
    jug_stats.base_attack_time = 1.4;
    jug_stats.attack_speed     = 115.0;

    UnitStats lina_stats;
    lina_stats.max_health       = 750.0;
    lina_stats.max_mana         = 700.0;
    lina_stats.attack_damage    = 48.0;
    lina_stats.base_armor       = 0.0;
    lina_stats.magic_resist     = 0.25;
    lina_stats.base_attack_time = 1.7;
    lina_stats.attack_speed     = 100.0;

    auto* lion = world.spawn("Lion",       Team::Radiant, lion_stats, {0.0,   0.0});
    auto* jug  = world.spawn("Juggernaut", Team::Dire,    jug_stats,  {150.0, 0.0});
    auto* lina = world.spawn("Lina",       Team::Radiant, lina_stats, {-50.0, 50.0});

    // --- 附加技能 ---
    auto* earth_spike = reg.instantiate("lion_earth_spike", *lion);
    auto* hex         = reg.instantiate("lion_hex", *lion);
    auto* finger      = reg.instantiate("lion_finger_of_death", *lion);

    auto* blade_fury  = reg.instantiate("juggernaut_blade_fury", *jug);
    auto* omnislash   = reg.instantiate("juggernaut_omnislash", *jug);
    auto* heal_ward   = reg.instantiate("juggernaut_healing_ward", *jug);

    auto* dragon_slave = reg.instantiate("lina_dragon_slave", *lina);
    auto* lsa          = reg.instantiate("lina_light_strike_array", *lina);
    auto* laguna       = reg.instantiate("lina_laguna_blade", *lina);

    // --- 事件日志 ---
    world.events().subscribe<AttackLandedEvent>(
        [&](AttackLandedEvent& e) {
            auto* src = world.find(e.attacker);
            auto* dst = world.find(e.victim);
            if (!src || !dst) return;
            std::printf("  [%.2fs] %s attacks %s for %.0f (hp %.0f/%.0f)\n",
                        world.time(), src->name().c_str(), dst->name().c_str(),
                        e.damage, dst->health(), dst->stats().max_health);
        });

    world.events().subscribe<UnitDiedEvent>(
        [&](UnitDiedEvent& e) {
            auto* victim = world.find(e.victim);
            auto* killer = world.find(e.killer);
            std::printf("  [%.2fs] *** %s DIES%s%s ***\n",
                        world.time(),
                        victim ? victim->name().c_str() : "?",
                        killer ? " killed by " : "",
                        killer ? killer->name().c_str() : "");
        });

    auto status = [&]() {
        std::printf("  Status — Lion: %.0f/%.0f HP | Juggernaut: %.0f/%.0f HP | Lina: %.0f/%.0f HP\n",
                    lion->health(), lion->stats().max_health,
                    jug->health(), jug->stats().max_health,
                    lina->health(), lina->stats().max_health);
    };

    // --- 决斗开始 ---
    log_header("Round 1: Lina opens with Dragon Slave + Light Strike Array");
    {
        CastTarget t;
        t.point = {150.0, 0.0};
        t.has_point = true;
        dragon_slave->order_cast(t, world);
    }
    world.advance(0.5);
    {
        CastTarget t;
        t.point = {150.0, 0.0};
        t.has_point = true;
        lsa->order_cast(t, world);
    }
    world.advance(0.5);
    status();

    log_header("Round 2: Lion Hex + Earth Spike on Juggernaut");
    {
        CastTarget t;
        t.unit = jug;
        hex->order_cast(t, world);
    }
    world.advance(0.2);
    {
        CastTarget t;
        t.unit = jug;
        earth_spike->order_cast(t, world);
    }
    world.advance(0.5);
    status();

    log_header("Round 3: Juggernaut recovers, pops Healing Ward + Blade Fury");
    world.advance(2.5);
    {
        CastTarget t;
        heal_ward->order_cast(t, world);
    }
    world.advance(0.35);
    {
        CastTarget t;
        blade_fury->order_cast(t, world);
    }
    // 旋转时自动攻击 Lina
    world.order_attack(*jug, *lina);
    world.advance(2.0);
    status();

    log_header("Round 4: Lion Finger of Death on Juggernaut");
    {
        CastTarget t;
        t.unit = jug;
        finger->order_cast(t, world);
    }
    world.advance(0.6);
    status();

    log_header("Round 5: Lina finishes with Laguna Blade");
    if (jug->alive()) {
        CastTarget t;
        t.unit = jug;
        laguna->order_cast(t, world);
        world.advance(0.5);
    }
    status();

    log_header("Round 6: Juggernaut's last stand — Omnislash on Lion");
    if (jug->alive()) {
        CastTarget t;
        t.unit = lion;
        omnislash->order_cast(t, world);
        world.advance(0.5);
    }
    status();

    // 让剩余的自动攻击结算
    log_header("Cleanup: auto-attacks until someone falls");
    world.order_attack(*lion, *jug);
    world.order_attack(*lina, *jug);
    for (int i = 0; i < 40; ++i) {
        world.advance(0.5);
        if (!jug->alive() || (!lion->alive() && !lina->alive())) break;
    }
    status();

    std::printf("\n--- DUEL COMPLETE (t=%.2fs) ---\n", world.time());
    if (jug->alive())
        std::printf("Winner: Juggernaut (Dire)\n");
    else
        std::printf("Winner: Lion + Lina (Radiant)\n");

    (void)earth_spike; (void)hex; (void)finger;
    (void)blade_fury; (void)omnislash; (void)heal_ward;
    (void)dragon_slave; (void)lsa; (void)laguna;

    return 0;
}
