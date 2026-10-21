// Growth algorithms. All of them are deterministic for a given seed and parameter set.
#pragma once

#include <unordered_map>
#include "bvh.h"
#include "params.h"
#include "plant_set.h"
#include "rng.h"

namespace plants {

struct SurfaceSample {
    Vec3 point;
    Vec3 normal;
    uint8_t tag = 0;
};

// Area-weighted random points on host triangles whose tag is in mask.
class SurfaceSampler {
public:
    SurfaceSampler(const TriangleBvh& bvh, uint32_t mask);
    bool empty() const { return total_ <= 0; }
    SurfaceSample sample(Rng& rng) const;

private:
    const TriangleBvh& bvh_;
    std::vector<float> cdf_;
    std::vector<int> tris_;
    float total_ = 0;
};

struct ClimberSeed {
    Vec3 point;
    Vec3 normal;
    bool onTop = false;  // started on a roof/ledge: spreads sideways and drapes over edges
};

// Climber roots: wall points near the ground, plus a few on top surfaces.
std::vector<ClimberSeed> pickClimberSeeds(const TriangleBvh& bvh, const Params& p, Rng& rng);

// Ivy-like growth that clings to the host, wraps around edges and drapes from ledges.
void growClimbers(const TriangleBvh& bvh, const Params& p, Rng& rng, const std::vector<ClimberSeed>& seeds,
                  PlantSet& out);

enum BushStyle { kBushRound = 0, kBushTall = 1, kBushSpreading = 2, kBushFlowering = 3, kBushStyleCount = 4 };

// One bush rooted at base, growing along up. Returns the group id it used.
int growBush(const Params& p, Rng& rng, const Vec3& base, const Vec3& up, float scale, int style, PlantSet& out);

// Bushes on the host: on the ground patch near walls and on roofs/ledges.
void scatterBushesOnHost(const TriangleBvh& bvh, const Params& p, Rng& rng, PlantSet& out);

// A tuft of grass blades (and sometimes a wild flower) at the origin.
void growGrassTuft(const Params& p, Rng& rng, PlantSet& out);

// Smooth "foliage cloud" normals and occlusion from clumps, shared by every LOD so they match.
void finalizeShading(const Params& p, PlantSet& set);

// Normal and occlusion of the clump field of one group at a point.
struct FieldSample {
    Vec3 normal;
    float ao = 1;
    bool inside = false;
};
class ClumpField {
public:
    explicit ClumpField(const std::vector<Clump>& clumps);
    FieldSample sample(const Vec3& p, int group, const Vec3& fallback) const;

private:
    struct Entry {
        Vec3 center, side, axis, fwd, invRadii;
        int group;
    };
    std::vector<Entry> entries_;
    float cell_ = 1.0f;
    std::unordered_map<uint64_t, std::vector<int>> buckets_;
    uint64_t key(int x, int y, int z) const;
};

// Frame used for a clump's ellipsoid: (side, axis, forward). Shared by shading and puff meshes.
Frame clumpFrame(const Clump& c);

}  // namespace plants
