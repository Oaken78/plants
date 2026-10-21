// Core tests. Built with em++ and run under node (see tools/build.ps1), so no native compiler is needed.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include "plants/api.h"
#include "plants/bvh.h"
#include "plants/plant_set.h"

using namespace plants;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

// A 4 x 3 x 4 m box standing on a 16 x 16 m ground plane.
struct TestHost {
    std::vector<float> pos;
    std::vector<uint32_t> idx;
    std::vector<uint8_t> tags;
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, uint8_t tag) {
        uint32_t base = uint32_t(pos.size() / 3);
        for (Vec3 v : {a, b, c, d}) { pos.push_back(v.x); pos.push_back(v.y); pos.push_back(v.z); }
        uint32_t t[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
        idx.insert(idx.end(), t, t + 6);
        tags.push_back(tag);
        tags.push_back(tag);
    }
    explicit TestHost(float y = 3) {
        quad({-8, 0, 8}, {8, 0, 8}, {8, 0, -8}, {-8, 0, -8}, kTagGround);
        float x = 2, z = 2;
        quad({-x, 0, z}, {x, 0, z}, {x, y, z}, {-x, y, z}, kTagSolid);      // +z
        quad({x, 0, -z}, {-x, 0, -z}, {-x, y, -z}, {x, y, -z}, kTagSolid);  // -z
        quad({x, 0, z}, {x, 0, -z}, {x, y, -z}, {x, y, z}, kTagSolid);      // +x
        quad({-x, 0, -z}, {-x, 0, z}, {-x, y, z}, {-x, y, -z}, kTagSolid);  // -x
        quad({-x, y, z}, {x, y, z}, {x, y, -z}, {-x, y, -z}, kTagSolid);    // roof
    }
};

static uint64_t hashMesh(PlantsContext* ctx, int lod) {
    uint64_t h = 1469598103934665603ull;
    int n = plants_mesh_vertex_count(ctx, lod) * 3;
    const float* p = plants_mesh_positions(ctx, lod);
    for (int i = 0; i < n; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &p[i], 4);
        h = (h ^ bits) * 1099511628211ull;
    }
    return h;
}

static void testBvh() {
    TestHost host;
    std::vector<Vec3> pos;
    for (size_t i = 0; i < host.pos.size(); i += 3) pos.push_back({host.pos[i], host.pos[i + 1], host.pos[i + 2]});
    TriangleBvh bvh;
    bvh.build(pos, host.idx, host.tags);
    SurfaceHit h;
    CHECK(bvh.closestPoint({0, 1.5f, 2.3f}, 1.0f, h, kMaskSolid));
    CHECK(std::fabs(h.distance - 0.3f) < 1e-4f);
    CHECK(h.normal.z > 0.99f);
    CHECK(!bvh.closestPoint({0, 1.5f, 5.0f}, 1.0f, h, kMaskSolid));
    CHECK(bvh.raycast({0, 10, 0}, {0, -1, 0}, 20, h));
    CHECK(std::fabs(h.point.y - 3.0f) < 1e-4f);
    CHECK(bvh.raycast({5, 10, 5}, {0, -1, 0}, 20, h, kMaskGround));
    CHECK(h.tag == kTagGround);
}

static void testGrowth() {
    TestHost host;
    PlantsContext* ctx = plants_create();
    plants_set_host(ctx, host.pos.data(), int(host.pos.size() / 3), host.idx.data(), int(host.idx.size()), host.tags.data());

    auto t0 = std::chrono::steady_clock::now();
    int leaves = plants_grow_on_host(ctx, 42);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("grow_on_host: %d leaves, %d nodes, %d clumps, %d bushes, %d climbers, %.1f ms\n", leaves,
                plants_stat(ctx, PLANTS_STAT_NODES), plants_stat(ctx, PLANTS_STAT_CLUMPS), plants_stat(ctx, PLANTS_STAT_BUSHES),
                plants_stat(ctx, PLANTS_STAT_CLIMBERS), ms);
    CHECK(leaves > 1000);
    CHECK(plants_stat(ctx, PLANTS_STAT_CLIMBERS) > 0);
    CHECK(plants_stat(ctx, PLANTS_STAT_BUSHES) > 0);

    int prevTris = 1 << 30;
    for (int lod = 0; lod < plants_lod_count(); ++lod) {
        int verts = plants_mesh_vertex_count(ctx, lod);
        int tris = plants_mesh_index_count(ctx, lod) / 3;
        std::printf("  lod%d: %d verts, %d tris\n", lod, verts, tris);
        CHECK(tris > 0);
        CHECK(tris < prevTris);
        prevTris = tris;
        const float* p = plants_mesh_positions(ctx, lod);
        const uint32_t* idx = plants_mesh_indices(ctx, lod);
        bool finite = true, inRange = true;
        for (int i = 0; i < verts * 3; ++i) finite = finite && std::isfinite(p[i]);
        for (int i = 0; i < tris * 3; ++i) inRange = inRange && idx[i] < uint32_t(verts);
        CHECK(finite);
        CHECK(inRange);
        const float* b = plants_mesh_bounds(ctx, lod);
        CHECK(b[4] < 16.0f);  // the tallest things are trees on the 3 m box (tree.height 7 x size <= 1.3, +15%)
    }

    // Everything stays around the 16 x 16 m ground patch (tree crowns may overhang its edge).
    const float* b = plants_mesh_bounds(ctx, 0);
    CHECK(b[0] > -13 && b[2] > -13 && b[3] < 13 && b[5] < 13);

    uint64_t first = hashMesh(ctx, 0);
    plants_grow_on_host(ctx, 42);
    CHECK(hashMesh(ctx, 0) == first);  // deterministic
    plants_grow_on_host(ctx, 43);
    CHECK(hashMesh(ctx, 0) != first);  // seed matters
    plants_destroy(ctx);
}

static void testStandalone() {
    PlantsContext* ctx = plants_create();
    for (int style = 0; style < 4; ++style) {
        int leaves = plants_make_bush(ctx, 7 + style, style, 1.0f);
        std::printf("bush style %d: %d leaves, lod tris %d/%d/%d/%d\n", style, leaves, plants_mesh_index_count(ctx, 0) / 3,
                    plants_mesh_index_count(ctx, 1) / 3, plants_mesh_index_count(ctx, 2) / 3,
                    plants_mesh_index_count(ctx, 3) / 3);
        CHECK(leaves > 200);
        CHECK(plants_mesh_index_count(ctx, 3) > 0);
        CHECK(plants_mesh_bounds(ctx, 0)[1] > -0.1f);  // nothing below the root
    }
    int blades = plants_make_grass(ctx, 3);
    CHECK(blades >= 3);
    CHECK(plants_mesh_index_count(ctx, 0) > 0);
    CHECK(plants_mesh_index_count(ctx, 3) == 0);

    int n = plants_param_count();
    CHECK(n > 10);
    plants_set_param(ctx, 0, 1e9f);
    CHECK(plants_get_param(ctx, 0) == plants_param_max(0));  // clamped
    plants_destroy(ctx);
}

// Lowest bark vertex of LOD 0 that lies next to the box walls (outside the box, within 1 m of it).
static float lowestBarkBesideWalls(PlantsContext* ctx) {
    const float* p = plants_mesh_positions(ctx, 0);
    const uint8_t* aux = plants_mesh_aux(ctx, 0);
    float lowest = 1e9f;
    for (int i = 0; i < plants_mesh_vertex_count(ctx, 0); ++i) {
        if (aux[i * 4 + 3] != kMatBark) continue;
        float x = std::fabs(p[i * 3]), y = p[i * 3 + 1], z = std::fabs(p[i * 3 + 2]);
        bool beside = std::max(x, z) > 2.0f && std::max(x, z) < 3.0f;
        if (beside) lowest = std::min(lowest, y);
    }
    return lowest;
}

static int paramId(const char* name) {
    for (int i = 0; i < plants_param_count(); ++i)
        if (std::strcmp(plants_param_name(i), name) == 0) return i;
    return -1;
}

static void testTrees() {
    PlantsContext* ctx = plants_create();
    float prevHeight = 0;
    for (float size : {0.45f, 1.0f, 1.8f}) {
        int leaves = plants_make_tree(ctx, 11, size);
        const float* b = plants_mesh_bounds(ctx, 0);
        std::printf("tree size %.2f: %d leaves, %.1f m tall, lod tris %d/%d/%d/%d\n", size, leaves, b[4],
                    plants_mesh_index_count(ctx, 0) / 3, plants_mesh_index_count(ctx, 1) / 3,
                    plants_mesh_index_count(ctx, 2) / 3, plants_mesh_index_count(ctx, 3) / 3);
        CHECK(leaves > 300);
        CHECK(b[4] > prevHeight);  // bigger size, taller tree
        prevHeight = b[4];
        CHECK(plants_mesh_index_count(ctx, 3) > 0);
        // LOD 3 keeps the trunk: some bark below the crown.
        const uint8_t* aux = plants_mesh_aux(ctx, 3);
        bool trunk = false;
        for (int i = 0; i < plants_mesh_vertex_count(ctx, 3); ++i) trunk = trunk || aux[i * 4 + 3] == kMatBark;
        CHECK(trunk);
    }

    // Only trees, on the roof: low roofs send roots down the walls, high roofs keep them up.
    for (const char* name : {"climber.seeds", "climber.roofSeeds", "bush.count", "tree.groundCount"})
        plants_set_param(ctx, paramId(name), 0);
    plants_set_param(ctx, paramId("tree.count"), 1);
    for (float roof : {3.0f, 20.0f}) {
        TestHost host(roof);
        plants_set_host(ctx, host.pos.data(), int(host.pos.size() / 3), host.idx.data(), int(host.idx.size()), host.tags.data());
        plants_grow_on_host(ctx, 5);
        float lowest = lowestBarkBesideWalls(ctx);
        std::printf("tree on a %.0f m roof: %d tree(s), lowest root beside the walls at %.2f m\n", roof,
                    plants_stat(ctx, PLANTS_STAT_TREES), lowest);
        CHECK(plants_stat(ctx, PLANTS_STAT_TREES) == 1);
        if (roof < 5) CHECK(lowest < 0.3f);       // reached the ground
        else CHECK(lowest > roof - 3.0f);         // stayed up on the roof
    }

    // Root branching: forks and rootlets add bark next to the walls.
    TestHost low(3.0f);
    plants_set_host(ctx, low.pos.data(), int(low.pos.size() / 3), low.idx.data(), int(low.idx.size()), low.tags.data());
    auto barkBesideWalls = [&]() {
        const float* p = plants_mesh_positions(ctx, 0);
        const uint8_t* aux = plants_mesh_aux(ctx, 0);
        int n = 0;
        for (int i = 0; i < plants_mesh_vertex_count(ctx, 0); ++i) {
            float m = std::max(std::fabs(p[i * 3]), std::fabs(p[i * 3 + 2]));
            if (aux[i * 4 + 3] == kMatBark && m > 2.0f && m < 3.0f && p[i * 3 + 1] < 2.8f) ++n;
        }
        return n;
    };
    plants_set_param(ctx, paramId("tree.rootBranching"), 0);
    plants_grow_on_host(ctx, 5);
    int plain = barkBesideWalls();
    plants_set_param(ctx, paramId("tree.rootBranching"), 1.5f);
    plants_grow_on_host(ctx, 5);
    int branched = barkBesideWalls();
    std::printf("root bark on the walls: %d vertices unbranched, %d branched\n", plain, branched);
    CHECK(branched > plain * 2);
    plants_destroy(ctx);
}

int main() {
    testBvh();
    testGrowth();
    testStandalone();
    testTrees();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
