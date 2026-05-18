#pragma once

#include "dota/projectile/projectile.hpp"

#include <memory>
#include <vector>

namespace dota {

class World;

// 拥有所有活动投射物. `advance(dt, world)` 推进每个投射物;
// 返回 false 的(销毁请求)会被擦除.
class ProjectileManager {
public:
    Projectile* spawn(std::unique_ptr<Projectile> p);

    void advance(double dt, World& world);

    std::size_t live_count() const { return live_.size(); }
    const std::vector<std::unique_ptr<Projectile>>& live() const { return live_; }

    void   set_world(World* w) { world_ = w; }
    World* world() const       { return world_; }

private:
    std::vector<std::unique_ptr<Projectile>> live_;
    World*   world_{nullptr};
    EntityId next_pid_{1};
};

} // namespace dota
