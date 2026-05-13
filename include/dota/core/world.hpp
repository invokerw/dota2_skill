#pragma once

#include "dota/core/event_bus.hpp"
#include "dota/core/types.hpp"
#include "dota/core/unit.hpp"

#include <memory>
#include <string>
#include <vector>

namespace dota {

// --- Canonical events (Stage 1) ---

struct UnitDiedEvent {
    EntityId victim;
    EntityId killer; // kInvalidEntityId if no attributed source
};

// Fired whenever a basic attack resolves (lands on target). Stage 1 uses raw
// physical damage; Stage 5 will replace this hand-rolled math with the damage
// pipeline.
struct AttackLandedEvent {
    EntityId attacker;
    EntityId victim;
    double   damage;
};

class World {
public:
    // Fixed-tick simulation at 30Hz.
    static constexpr double kTickRate = 30.0;
    static constexpr double kTickDt   = 1.0 / kTickRate;

    World();

    // Creates a unit in the world; returns a non-owning pointer that is stable
    // for the lifetime of the World.
    Unit* spawn(std::string name, Team team, UnitStats stats, Vec2 position = {});

    Unit*       find(EntityId id);
    const Unit* find(EntityId id) const;

    std::vector<Unit*> units_on_team(Team team);
    std::vector<Unit*> enemies_of(const Unit& u);

    // Returns living enemies of `source` within `radius` of `origin`.
    std::vector<Unit*> find_enemies_in_radius(Vec2 origin, double radius, Team source_team);

    EventBus&       events()       { return events_; }
    const EventBus& events() const { return events_; }

    // Elapsed simulation time in seconds.
    double time() const { return time_; }

    // Step forward by `dt` seconds. Internally subdivides into kTickDt slices.
    void advance(double dt);

    // Issue a basic-attack order. Stage 1: attacker auto-attacks `target` every
    // time its attack cooldown reaches zero, as long as both remain alive. The
    // actual swing happens inside `advance()`.
    void order_attack(Unit& attacker, Unit& target);
    void stop_attack(Unit& attacker);

private:
    struct AttackOrder {
        EntityId attacker;
        EntityId target;
    };

    void tick_once();
    void resolve_attack(Unit& attacker, Unit& target);

    std::vector<std::unique_ptr<Unit>> units_;
    std::vector<AttackOrder>           orders_;
    EventBus events_;
    EntityId next_id_{1};
    double   time_{0.0};
};

} // namespace dota
