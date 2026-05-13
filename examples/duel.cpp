#include "dota/core/world.hpp"

#include <cstdio>

// Stage 1 demo: two units auto-attack until one dies. Prints a combat log.

int main() {
    dota::World world;

    dota::UnitStats lion_stats;
    lion_stats.max_health       = 560.0;
    lion_stats.attack_damage    = 52.0;
    lion_stats.base_armor       = 1.0;
    lion_stats.base_attack_time = 1.7;
    lion_stats.attack_speed     = 100.0;

    dota::UnitStats jug_stats;
    jug_stats.max_health       = 620.0;
    jug_stats.attack_damage    = 60.0;
    jug_stats.base_armor       = 4.0;
    jug_stats.base_attack_time = 1.4;
    jug_stats.attack_speed     = 115.0;

    auto* lion = world.spawn("Lion",       dota::Team::Radiant, lion_stats, {0.0,   0.0});
    auto* jug  = world.spawn("Juggernaut", dota::Team::Dire,    jug_stats,  {100.0, 0.0});

    world.events().subscribe<dota::AttackLandedEvent>(
        [&](dota::AttackLandedEvent& e) {
            auto* src = world.find(e.attacker);
            auto* dst = world.find(e.victim);
            if (!src || !dst) return;
            std::printf("[t=%.2fs] %s hits %s for %.1f (hp %.1f/%.1f)\n",
                        world.time(), src->name().c_str(), dst->name().c_str(),
                        e.damage, dst->health(), dst->stats().max_health);
        });

    world.events().subscribe<dota::UnitDiedEvent>(
        [&](dota::UnitDiedEvent& e) {
            auto* victim = world.find(e.victim);
            auto* killer = world.find(e.killer);
            std::printf("[t=%.2fs] %s was killed%s%s\n",
                        world.time(),
                        victim ? victim->name().c_str() : "?",
                        killer ? " by " : "",
                        killer ? killer->name().c_str() : "");
        });

    world.order_attack(*lion, *jug);
    world.order_attack(*jug, *lion);

    // Step forward until one dies or we timeout.
    for (int i = 0; i < 60; ++i) {
        world.advance(0.5);
        if (!lion->alive() || !jug->alive()) break;
    }
    return 0;
}
