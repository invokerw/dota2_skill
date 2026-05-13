#include "dota/modifier/modifier.hpp"

namespace dota {

Modifier::Modifier(std::string name, Unit& owner, double duration)
    : name_(std::move(name))
    , owner_(owner)
    , duration_(duration)
    , permanent_(duration < 0.0) {}

void Modifier::set_stack_count(int n) {
    if (n == stack_count_) return;
    const int old = stack_count_;
    stack_count_ = n;
    on_stack_changed(old, stack_count_);
}

void Modifier::advance(double dt) {
    if (!permanent_) duration_ -= dt;
    if (think_interval_ > 0.0) {
        think_accum_ += dt;
        // Small epsilon so that boundaries like dt=1/30 aggregated over the
        // interval don't lose a tick to float drift.
        constexpr double kEps = 1e-6;
        while (think_accum_ + kEps >= think_interval_) {
            think_accum_ -= think_interval_;
            on_interval_think();
        }
    }
}

} // namespace dota
