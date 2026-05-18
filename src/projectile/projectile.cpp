#include "dota/projectile/projectile.hpp"

#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"

#include <algorithm>
#include <cmath>

namespace dota {

// --- LinearProjectile

LinearProjectile::LinearProjectile(const Params& p)
    : dir_(normalized(p.direction))
    , speed_(p.speed)
    , total_length_(p.length)
    , remaining_distance_(p.length)
    , width_(p.width)
    , destroy_on_first_hit_(p.destroy_on_first_hit) {
    pos_         = p.origin;
    source_id_   = p.source_id;
    source_team_ = p.source_team;
}

bool LinearProjectile::advance(double dt, World& world) {
    if (remaining_distance_ <= 0.0 || speed_ <= 0.0) {
        if (on_finish_) on_finish_();
        ProjectileFinishedEvent fin{pid_};
        world.events().publish(fin);
        return false;
    }
    const double step = std::min(speed_ * dt, remaining_distance_);
    const Vec2 prev = pos_;
    Vec2 next{pos_.x + dir_.x * step, pos_.y + dir_.y * step};

    // 沿 (prev, next) 线段扫描敌人; 过滤已命中.
    auto candidates = world.find_enemies_in_line(prev, next, width_, source_team_);
    for (Unit* u : candidates) {
        if (already_hit_.count(u->id())) continue;
        if (u->modifiers().has_state(ModifierState::Invulnerable)) {
            already_hit_.insert(u->id());
            continue;
        }
        ProjectileHitEvent hit{pid_, u->id(), u->position()};
        world.events().publish(hit);
        if (on_hit_) on_hit_(*u, u->position());
        already_hit_.insert(u->id());
        if (destroy_on_first_hit_) {
            pos_ = next;
            if (on_finish_) on_finish_();
            ProjectileFinishedEvent fin{pid_};
            world.events().publish(fin);
            return false;
        }
    }

    pos_ = next;
    remaining_distance_ -= step;
    if (remaining_distance_ <= 0.0) {
        if (on_finish_) on_finish_();
        ProjectileFinishedEvent fin{pid_};
        world.events().publish(fin);
        return false;
    }
    return true;
}

// --- TrackingProjectile

TrackingProjectile::TrackingProjectile(const Params& p)
    : target_id_(p.target_id)
    , speed_(p.speed)
    , dodgeable_(p.dodgeable) {
    pos_         = p.origin;
    source_id_   = p.source_id;
    source_team_ = p.source_team;
    (void)dodgeable_;
}

bool TrackingProjectile::advance(double dt, World& world) {
    Unit* target = world.find(target_id_);
    if (!target || !target->alive()) {
        if (on_finish_) on_finish_();
        ProjectileFinishedEvent fin{pid_};
        world.events().publish(fin);
        return false;
    }
    // Untargetable / OutOfGame 中途逃逸 → fizzle
    if (target->modifiers().has_state(ModifierState::Untargetable) ||
        target->modifiers().has_state(ModifierState::OutOfGame)) {
        if (on_finish_) on_finish_();
        ProjectileFinishedEvent fin{pid_};
        world.events().publish(fin);
        return false;
    }

    const Vec2 to_target = target->position() - pos_;
    const double dist = length(to_target);
    const double step = speed_ * dt;
    if (dist <= step + 1e-6) {
        pos_ = target->position();
        ProjectileHitEvent hit{pid_, target->id(), pos_};
        world.events().publish(hit);
        if (on_hit_) on_hit_(*target, pos_);
        ProjectileFinishedEvent fin{pid_};
        world.events().publish(fin);
        return false;
    }
    const Vec2 dir = normalized(to_target);
    pos_.x += dir.x * step;
    pos_.y += dir.y * step;
    return true;
}

} // namespace dota
