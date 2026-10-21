// Where plants start: climber roots and bush placement on an arbitrary host mesh.
#include "plants/generator.h"

namespace plants {

static const Vec3 kUp{0, 1, 0};
static const Vec3 kDown{0, -1, 0};

SurfaceSampler::SurfaceSampler(const TriangleBvh& bvh, uint32_t mask) : bvh_(bvh) {
    for (int i = 0; i < bvh.triangleCount(); ++i) {
        const auto& t = bvh.triangle(i);
        if (!((1u << t.tag) & mask)) continue;
        total_ += t.area;
        cdf_.push_back(total_);
        tris_.push_back(i);
    }
}

SurfaceSample SurfaceSampler::sample(Rng& rng) const {
    SurfaceSample s;
    if (total_ <= 0) return s;
    float r = rng.uniform() * total_;
    size_t k = std::lower_bound(cdf_.begin(), cdf_.end(), r) - cdf_.begin();
    if (k >= tris_.size()) k = tris_.size() - 1;
    const auto& t = bvh_.triangle(tris_[k]);
    float u = rng.uniform(), v = rng.uniform();
    if (u + v > 1) { u = 1 - u; v = 1 - v; }
    s.point = t.a + (t.b - t.a) * u + (t.c - t.a) * v;
    s.normal = t.normal;
    s.tag = t.tag;
    return s;
}

static bool tooClose(const std::vector<ClimberSeed>& seeds, const Vec3& p, float minDist) {
    for (const auto& s : seeds)
        if (length2(s.point - p) < minDist * minDist) return true;
    return false;
}

std::vector<ClimberSeed> pickClimberSeeds(const TriangleBvh& bvh, const Params& p, Rng& rng) {
    std::vector<ClimberSeed> seeds;
    SurfaceSampler solid(bvh, kMaskSolid);
    if (solid.empty()) return seeds;
    const float offset = 0.03f;
    const float floorY = bvh.bounds().lo.y;

    // Wall roots: a vertical wall point with walkable ground just below it.
    int wanted = p.i(kParamClimberSeeds);
    for (int attempt = 0; attempt < wanted * 80 && int(seeds.size()) < wanted; ++attempt) {
        SurfaceSample s = solid.sample(rng);
        if (std::fabs(s.normal.y) > 0.35f) continue;
        SurfaceHit g;
        if (!bvh.raycast(s.point + s.normal * 0.08f, kDown, 0.9f, g, kMaskGround | kMaskSolid)) continue;
        if (g.normal.y < 0.7f) continue;
        Vec3 root = s.point + s.normal * offset;
        root.y = g.point.y + 0.1f;
        if (tooClose(seeds, root, 1.3f)) continue;
        seeds.push_back({root, s.normal, false});
    }

    // Roof roots: spread over flat tops and drape over their edges.
    int roofWanted = p.i(kParamClimberRoofSeeds);
    int roofPlaced = 0;
    for (int attempt = 0; attempt < roofWanted * 80 && roofPlaced < roofWanted; ++attempt) {
        SurfaceSample s = solid.sample(rng);
        if (s.normal.y < 0.85f || s.point.y < floorY + 1.2f) continue;
        Vec3 root = s.point + s.normal * offset;
        if (tooClose(seeds, root, 1.6f)) continue;
        seeds.push_back({root, s.normal, true});
        ++roofPlaced;
    }
    return seeds;
}

struct PlacedBush {
    Vec3 pos;
    float radius;
};

static bool overlaps(const std::vector<PlacedBush>& placed, const Vec3& p, float r) {
    for (const auto& b : placed) {
        float m = (b.radius + r) * 0.62f;
        if (length2(b.pos - p) < m * m) return true;
    }
    return false;
}

static bool nearTrunk(const std::vector<Vec3>& trunks, const Vec3& p, float r) {
    for (const auto& t : trunks) {
        float m = r * 0.6f + 0.5f;
        if (length2(projectOnPlane(t - p, kUp)) < m * m) return true;
    }
    return false;
}

static int pickStyle(Rng& rng, float flowering, bool onRoof) {
    if (rng.chance(flowering)) return kBushFlowering;
    float r = rng.uniform();
    if (onRoof) return r < 0.6f ? kBushRound : kBushSpreading;
    if (r < 0.5f) return kBushRound;
    if (r < 0.75f) return kBushTall;
    return kBushSpreading;
}

void scatterBushesOnHost(const TriangleBvh& bvh, const Params& p, Rng& rng, const std::vector<Vec3>& avoid, PlantSet& out) {
    int total = p.i(kParamBushCount);
    int roofCount = int(total * p[kParamBushRoofShare] + 0.5f);
    int groundCount = total - roofCount;
    const float baseRadius = p[kParamBushRadius];
    std::vector<PlacedBush> placed;

    // Ground bushes, preferring the foot of walls.
    SurfaceSampler ground(bvh, kMaskGround);
    int placedGround = 0;
    for (int attempt = 0; !ground.empty() && attempt < groundCount * 60 && placedGround < groundCount; ++attempt) {
        SurfaceSample s = ground.sample(rng);
        if (s.normal.y < 0.7f) continue;
        SurfaceHit wall;
        float d = bvh.closestPoint(s.point + kUp * 0.4f, 5.0f, wall, kMaskSolid | kMaskNoGrow) ? wall.distance : 5.0f;
        float preference = d < 2.2f ? 1.0f : saturate(1.0f - (d - 2.2f) / 3.0f) * 0.7f + 0.05f;
        if (!rng.chance(preference)) continue;
        float scale = rng.range(0.6f, 1.3f);
        float r = baseRadius * scale;
        Vec3 base = s.point;
        if (d < r * 0.7f) {
            // Nudge the bush out so it leans against the wall instead of passing through it.
            Vec3 away = normalize(projectOnPlane(s.point - wall.point, kUp), s.normal);
            base = base + away * (r * 0.7f - d);
            SurfaceHit g;
            if (!bvh.raycast(base + kUp * 1.0f, kDown, 2.0f, g, kMaskGround | kMaskSolid) || g.tag != kTagGround) continue;
            base = g.point;
        }
        if (overlaps(placed, base, r) || nearTrunk(avoid, base, r)) continue;
        Rng bushRng = rng.fork(uint32_t(1000 + placed.size()));
        growBush(p, bushRng, base - kUp * 0.03f, kUp, scale, pickStyle(rng, p[kParamBushFlowering], false), out);
        placed.push_back({base, r});
        ++placedGround;
    }

    // Roof and ledge bushes: need head room and a supporting surface under the whole crown.
    SurfaceSampler solid(bvh, kMaskSolid);
    const float floorY = bvh.bounds().lo.y;
    int placedRoof = 0;
    for (int attempt = 0; !solid.empty() && attempt < roofCount * 120 && placedRoof < roofCount; ++attempt) {
        SurfaceSample s = solid.sample(rng);
        if (s.normal.y < 0.85f || s.point.y < floorY + 0.8f) continue;
        SurfaceHit h;
        if (bvh.raycast(s.point + kUp * 0.02f, kUp, 1.2f, h)) continue;
        float scale = rng.range(0.4f, 0.85f);
        bool supported = false;
        for (int tries = 0; tries < 2 && !supported; ++tries) {
            float r = baseRadius * scale * 0.55f;
            supported = true;
            for (int k = 0; k < 6 && supported; ++k) {
                float a = k * (2 * kPi / 6);
                Vec3 probe = s.point + Vec3{std::cos(a) * r, 0.25f, std::sin(a) * r};
                SurfaceHit g;
                supported = bvh.raycast(probe, kDown, 0.5f, g, kMaskSolid) && g.normal.y > 0.7f;
            }
            if (!supported) scale *= 0.6f;
        }
        if (!supported) continue;
        float r = baseRadius * scale;
        if (overlaps(placed, s.point, r) || nearTrunk(avoid, s.point, r)) continue;
        Rng bushRng = rng.fork(uint32_t(5000 + placed.size()));
        growBush(p, bushRng, s.point - kUp * 0.02f, kUp, scale, pickStyle(rng, p[kParamBushFlowering], true), out);
        placed.push_back({s.point, r});
        ++placedRoof;
    }
}

}  // namespace plants
