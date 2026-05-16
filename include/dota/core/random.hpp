#pragma once

#include <cstdint>

namespace dota {

// 简易确定性 RNG（xorshift64）。固定种子以保证测试可复现。
// 用于 evasion 判定、未来的暴击/触发等。
class Rng {
public:
    explicit Rng(std::uint64_t seed = 0xDEADBEEFCAFEBABEull) : state_(seed ? seed : 1) {}

    void reseed(std::uint64_t seed) { state_ = seed ? seed : 1; }

    std::uint64_t next_u64() {
        std::uint64_t x = state_;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        state_ = x;
        return x;
    }

    // 返回 [0, 1) 区间均匀随机值。
    double next_double() {
        // 取高 53 位映射到 [0,1)。
        return (next_u64() >> 11) * (1.0 / (1ull << 53));
    }

    // 概率检定：p ∈ [0,1]，p<=0 返回 false，p>=1 返回 true。
    bool chance(double p) {
        if (p <= 0.0) return false;
        if (p >= 1.0) return true;
        return next_double() < p;
    }

private:
    std::uint64_t state_;
};

} // namespace dota
