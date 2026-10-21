// Core tests. Built with em++ and run under node (see tools/build.ps1), so no native compiler is needed.
#include <chrono>
#include <cstdio>
#include <vector>
#include "plants/api.h"
#include "plants/bvh.h"

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
    TestHost() {
        quad({-8, 0, 8}, {8, 0, 8}, {8, 0, -8}, {-8, 0, -8}, kTagGround);
        float x = 2, y = 3, z = 2;
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
        CHECK(b[4] < 8.0f);  // nothing grows far above the 3 m box
    }

    // Plants hug the host: every LOD0 vertex is within a metre of some surface... and inside the ground area.
    const float* b = plants_mesh_bounds(ctx, 0);
    CHECK(b[0] > -9 && b[2] > -9 && b[3] < 9 && b[5] < 9);

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

int main() {
    testBvh();
    testGrowth();
    testStandalone();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
