// Deterministic PCG32 random generator: the same seed gives the same plant on every platform.
#pragma once

#include "math.h"

namespace plants {

class Rng {
public:
    explicit Rng(uint64_t seed = 1, uint64_t stream = 0x9e3779b97f4a7c15ull) {
        inc_ = (stream << 1u) | 1u;
        next();
        state_ += seed;
        next();
    }

    uint32_t next() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31));
    }

    float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    int rangeInt(int lo, int hiInclusive) { return lo + static_cast<int>(next() % static_cast<uint32_t>(hiInclusive - lo + 1)); }
    bool chance(float p) { return uniform() < p; }
    // Symmetric jitter: value * (1 +- amount).
    float jitter(float value, float amount) { return value * (1.0f + range(-amount, amount)); }

    Vec3 unitVector() {
        float z = range(-1.0f, 1.0f);
        float a = range(0.0f, 2.0f * kPi);
        float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return {r * std::cos(a), z, r * std::sin(a)};
    }

    Vec3 inUnitSphere() {
        Vec3 d = unitVector();
        return d * std::cbrt(uniform());
    }

    // Derive an independent generator, so adding draws to one system does not change another.
    Rng fork(uint32_t salt) { return Rng(next() ^ (uint64_t(salt) * 0x2545F4914F6CDD1Dull), salt + 7); }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 1;
};

}  // namespace plants
