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

// True when p is inside the host: there is something overhead and nearly every horizontal direction is blocked.
// Plants never start in such places (the floor under a house, the inside of a roof hut); roots may still crawl in.
bool isEnclosed(const TriangleBvh& bvh, const Vec3& p);

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

// One bush rooted at base, growing along up. With a host, its clumps keep out of walls and no leaf or flower ends
// up behind one. Returns the group id it used.
int growBush(const Params& p, Rng& rng, const Vec3& base, const Vec3& up, float scale, int style, PlantSet& out,
             const TriangleBvh* host = nullptr);

// Removes leaves and flowers from index firstLeaf / firstFlower on that sit behind a host surface as seen from the
// plant's own axis (base + up * height): foliage that pushed through a wall.
void dropFoliageBehindWalls(const TriangleBvh& host, const Vec3& base, const Vec3& up, size_t firstLeaf,
                            size_t firstFlower, PlantSet& out);

// Leaves over the outer surface of the clumps [firstClump, end): the shared foliage of bushes and trees.
// aoFrom/aoTo are heights along up where the foliage starts and ends (darker and earlier at the bottom).
void coverClumpsWithLeaves(Rng& rng, PlantSet& out, size_t firstClump, const Vec3& base, const Vec3& up, float aoFrom,
                           float aoTo, float leafSize, float density, uint8_t mat, uint8_t shape, int group,
                           float birthStart);

// Bushes on the host: on the ground patch near walls and on roofs/ledges, keeping clear of tree trunks.
void scatterBushesOnHost(const TriangleBvh& bvh, const Params& p, Rng& rng, const std::vector<Vec3>& avoid, PlantSet& out);

enum TreeSpecies { kTreeRound = 0, kTreeOak = 1, kTreeSpeciesCount = 2 };

// A tree rooted at base. With a host, it leans toward open sky, fits under overhangs, and its roots follow the
// host's surfaces; roots of a tree less than tree.rootReach above the ground climb down to reach it.
// size scales tree.height or oak.height (about 0.4 small, 1 medium, 1.8 large). crowding is 0 for a tree standing
// alone and 1 for one hemmed in by trees or walls: crowded trees grow taller and narrower. Returns the group id.
int growTree(const Params& p, Rng& rng, const TriangleBvh* host, const Vec3& base, float size, int species, float crowding,
             PlantSet& out);

// How hemmed in a tree at base would be (0..1), from nearby walls of the host and the other trunks.
float estimateCrowding(const TriangleBvh& bvh, const Vec3& base, const std::vector<Vec3>& trunks);

// Trees on the host: on roofs and ledges (preferring spots near an edge) and on the ground patch; tree.oakShare of
// them are oaks. Each tree's crowding is estimated from the walls and the trees placed before it.
// Appends each trunk position to trunks.
void scatterTreesOnHost(const TriangleBvh& bvh, const Params& p, Rng& rng, PlantSet& out, std::vector<Vec3>& trunks);

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
