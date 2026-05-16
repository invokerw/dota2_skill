#include "dota/projectile/manager.hpp"

#include <algorithm>

namespace dota {

Projectile* ProjectileManager::spawn(std::unique_ptr<Projectile> p) {
    Projectile* raw = p.get();
    live_.push_back(std::move(p));
    return raw;
}

void ProjectileManager::advance(double dt, World& world) {
    if (live_.empty()) return;
    // 快照指针：advance 回调可能 spawn 新投射物（追加到 live_ 末尾）。
    const std::size_t n = live_.size();
    std::vector<bool> keep(n, true);
    for (std::size_t i = 0; i < n; ++i) {
        if (!live_[i]) { keep[i] = false; continue; }
        keep[i] = live_[i]->advance(dt, world);
    }
    // 销毁旧批次中标记为 false 的（保留同 tick 中新 spawn 的 i >= n）。
    std::vector<std::unique_ptr<Projectile>> remaining;
    remaining.reserve(live_.size());
    for (std::size_t i = 0; i < live_.size(); ++i) {
        if (i < n && !keep[i]) continue;
        if (live_[i]) remaining.push_back(std::move(live_[i]));
    }
    live_ = std::move(remaining);
}

} // namespace dota
