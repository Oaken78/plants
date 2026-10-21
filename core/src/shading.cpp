// Foliage "cloud" shading.
//
// Anime foliage reads as soft masses, not as thousands of individually lit leaves. Each leaf's
// lighting normal is the gradient of a smooth field built from its plant's clumps, so a bush shades
// like a few lumpy spheres. The distant puff LODs sample the same field, which keeps lighting
// identical across LOD transitions.
#include "plants/generator.h"

namespace plants {

Frame clumpFrame(const Clump& c) { return makeFrame(c.axis, anyPerpendicular(c.axis)); }

static const float kInfluence = 1.6f;  // field reaches 1.6x a clump's radius

ClumpField::ClumpField(const std::vector<Clump>& clumps) {
    float maxR = 0.1f;
    for (const auto& c : clumps) maxR = std::max(maxR, std::max(c.radii.x, std::max(c.radii.y, c.radii.z)));
    cell_ = std::max(0.5f, maxR * kInfluence);
    entries_.reserve(clumps.size());
    for (size_t i = 0; i < clumps.size(); ++i) {
        const Clump& c = clumps[i];
        Frame f = clumpFrame(c);
        entries_.push_back({c.center, f.side, f.normal, f.forward,
                            Vec3{1.0f / c.radii.x, 1.0f / c.radii.y, 1.0f / c.radii.z}, c.group});
        float reach = std::max(c.radii.x, std::max(c.radii.y, c.radii.z)) * kInfluence;
        int x0 = int(std::floor((c.center.x - reach) / cell_)), x1 = int(std::floor((c.center.x + reach) / cell_));
        int y0 = int(std::floor((c.center.y - reach) / cell_)), y1 = int(std::floor((c.center.y + reach) / cell_));
        int z0 = int(std::floor((c.center.z - reach) / cell_)), z1 = int(std::floor((c.center.z + reach) / cell_));
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z) buckets_[key(x, y, z)].push_back(int(i));
    }
}

uint64_t ClumpField::key(int x, int y, int z) const {
    auto c = [](int v) { return uint64_t(uint32_t(v + (1 << 20)) & 0x1fffff); };
    return (c(x) << 42) | (c(y) << 21) | c(z);
}

FieldSample ClumpField::sample(const Vec3& p, int group, const Vec3& fallback) const {
    FieldSample out;
    out.normal = fallback;
    auto it = buckets_.find(key(int(std::floor(p.x / cell_)), int(std::floor(p.y / cell_)), int(std::floor(p.z / cell_))));
    if (it == buckets_.end()) return out;
    Vec3 grad;
    float minS = 1e9f;
    for (int idx : it->second) {
        const Entry& e = entries_[idx];
        if (e.group != group) continue;
        Vec3 d = p - e.center;
        Vec3 q{dot(d, e.side) * e.invRadii.x, dot(d, e.axis) * e.invRadii.y, dot(d, e.fwd) * e.invRadii.z};
        float s2 = length2(q);
        if (s2 > kInfluence * kInfluence) continue;
        float s = std::sqrt(s2);
        float w = 1.0f - s2 / (kInfluence * kInfluence);
        w *= w;
        // Gradient of the ellipsoidal distance, normalised so every clump votes with unit length.
        Vec3 g = normalize(e.side * (q.x * e.invRadii.x) + e.axis * (q.y * e.invRadii.y) + e.fwd * (q.z * e.invRadii.z),
                           e.axis);
        grad += g * w;
        minS = std::min(minS, s);
    }
    if (minS > 1e8f) return out;
    out.normal = normalize(grad, fallback);
    out.ao = smoothstepf(0.3f, 1.05f, minS);
    out.inside = minS < 1.0f;
    return out;
}

void finalizeShading(const Params& p, PlantSet& set) {
    const float blend = p[kParamShadeBlend];
    ClumpField field(set.clumps);
    for (Leaf& leaf : set.leaves) {
        FieldSample fs = field.sample(leaf.pos, leaf.group, leaf.normal);
        leaf.shadeNormal = normalize(lerp(leaf.normal, fs.normal, blend), leaf.normal);
        leaf.ao *= lerpf(0.45f, 1.0f, fs.ao);
    }
    for (Flower& f : set.flowers) {
        FieldSample fs = field.sample(f.pos, f.group, f.normal);
        f.shadeNormal = normalize(lerp(f.normal, fs.normal, 0.5f), f.normal);
    }
}

}  // namespace plants
