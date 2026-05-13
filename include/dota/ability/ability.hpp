#pragma once

#include "dota/ability/behavior.hpp"
#include "dota/core/types.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace dota {

class Unit;
class World;

// Lifecycle phases for an ability cast. Mirrors Dota's Pre-cast → Cast Point →
// Cast → Backswing → Cooldown chain. Passive abilities remain in Ready.
enum class CastPhase : std::uint8_t {
    Ready = 0,
    Casting,     // cast-point animation; can be interrupted
    Backswing,   // post-cast animation; does not block new casts
    Channelling, // channelled only
    OnCooldown,
};

// Reasons a cast can fail legality. Useful for UI/debug; tests assert on these.
enum class CastError : std::uint8_t {
    None = 0,
    NotReady,
    OnCooldown,
    NotEnoughMana,
    Silenced,
    Stunned,
    Hexed,
    CasterDead,
    InvalidTarget,
    TargetMagicImmune,
    OutOfRange,
    NotLearned,
};

// Per-level scalar. We model `ability_special` as a dictionary of name → per-
// level vector (int or float). Lookup picks `values[min(level-1, size-1)]`.
struct AbilitySpecialValue {
    // One of:
    std::vector<double> floats;
    std::vector<long>   ints;
    bool is_int = false;

    double get_float(int level) const;
    long   get_int(int level) const;
};

using AbilitySpecial = std::unordered_map<std::string, AbilitySpecialValue>;

// Target passed when ordering a cast. Unused fields are ignored based on
// BehaviorFlag.
struct CastTarget {
    Unit* unit       = nullptr;
    Vec2  point      = {};
    bool  has_point  = false;
};

// Context handed to subclasses when the cast actually resolves. Kept
// deliberately tiny — more fields can be added as the pipeline grows.
struct CastContext {
    Unit*      caster   = nullptr;
    World*     world    = nullptr;
    CastTarget target;
    int        level    = 1;
};

// Abstract base for all abilities — shared by DataDriven (YAML) and Scripted
// (Lua in Stage 4). Lifetime: owned by AbilityManager on the caster.
class Ability {
public:
    Ability(std::string name,
            std::uint32_t behavior,
            TargetTeam target_team,
            Unit& caster);
    virtual ~Ability() = default;

    Ability(const Ability&) = delete;
    Ability& operator=(const Ability&) = delete;

    // --- Static metadata ---
    const std::string& name() const { return name_; }
    std::uint32_t      behavior() const { return behavior_; }
    TargetTeam         target_team() const { return target_team_; }

    Unit&              caster()       { return caster_; }
    const Unit&        caster() const { return caster_; }

    // --- Per-level fields ---
    int  level() const { return level_; }
    void set_level(int l) { level_ = std::max(1, l); }

    double cast_point()    const { return cast_point_; }
    double backswing()     const { return backswing_; }
    double channel_time()  const { return channel_time_; }
    double cast_range()    const { return cast_range_; }
    double cooldown_for_level() const;
    double mana_cost_for_level() const;

    // Used by DataDriven loader to populate from YAML. Scripted abilities can
    // call the same setters directly from Lua.
    void set_cast_point(double t)   { cast_point_ = t; }
    void set_backswing(double t)    { backswing_ = t; }
    void set_channel_time(double t) { channel_time_ = t; }
    void set_cast_range(double r)   { cast_range_ = r; }
    void set_cooldown_levels(std::vector<double> v)  { cooldowns_ = std::move(v); }
    void set_mana_cost_levels(std::vector<double> v) { mana_costs_ = std::move(v); }
    void set_ability_special(AbilitySpecial s)       { special_ = std::move(s); }

    const AbilitySpecial& ability_special() const { return special_; }

    // --- Runtime state ---
    CastPhase   phase()        const { return phase_; }
    double      phase_timer()  const { return phase_timer_; }
    double      cooldown_remaining() const { return cooldown_; }
    bool        is_passive()   const { return has_flag(behavior_, BehaviorFlag::Passive); }
    bool        is_channelled()const { return has_flag(behavior_, BehaviorFlag::Channelled); }

    // Legality check without state mutation. Populates `err` with the first
    // failure reason found.
    CastError can_cast(const CastTarget& target) const;

    // Issue a cast order. Returns CastError::None on success. The actual
    // `on_spell_start` fires after cast_point inside World::advance().
    CastError order_cast(const CastTarget& target, World& world);

    // Advance cast/channel/backswing/cooldown timers by dt. World drives this
    // every tick. Handles interruption when stunned/silenced mid-cast.
    void advance(double dt);

    // --- Subclass hooks ---
    // Called when the cast point completes successfully (i.e. not interrupted).
    virtual void on_spell_start(CastContext&) = 0;
    // For channelled abilities: fires each tick while channelling.
    virtual void on_channel_think(CastContext&, double /*dt*/) {}
    // For channelled abilities: fires once when channel ends.
    virtual void on_channel_finish(CastContext&, bool /*interrupted*/) {}
    // Optional: upgrade hook.
    virtual void on_upgrade(int /*new_level*/) {}

protected:
    // Subclasses may override to allow bespoke target validation (e.g.
    // creatable-only targeting in Stage 6).
    virtual CastError validate_target(const CastTarget&) const;

private:
    void enter_phase(CastPhase p, double timer);
    bool current_target_still_valid() const;

    std::string name_;
    std::uint32_t behavior_;
    TargetTeam    target_team_;
    Unit&         caster_;

    int level_ = 1;

    // Timing fields (all seconds).
    double cast_point_   = 0.0;
    double backswing_    = 0.0;
    double channel_time_ = 0.0;
    double cast_range_   = 0.0;

    std::vector<double> cooldowns_;   // per level
    std::vector<double> mana_costs_;  // per level
    AbilitySpecial      special_;

    // Runtime.
    CastPhase phase_       = CastPhase::Ready;
    double    phase_timer_ = 0.0;
    double    cooldown_    = 0.0;
    World*    world_       = nullptr; // valid while casting/backswing
    CastTarget pending_target_{};
};

} // namespace dota
