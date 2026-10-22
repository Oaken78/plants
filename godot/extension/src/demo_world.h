// DemoWorld: the demo's procedural layout, ported line by line from web/js/world.js.
//
// Rolling terrain, a town of overgrown houses and towers, groves, bushes and grass, all pure functions of position,
// so cells can be generated in any order. The integer hash reproduces the JavaScript one bit for bit (Math.imul is
// a wrapping 32-bit multiply), so the Godot town is the same town as the web demo's and the viewpoints match.
#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref_counted.hpp>

namespace overgrowth {

struct Instance {
    float x, y, z, yaw, scale;
    int variant;
    float cos_scaled = 0, sin_scaled = 0;  // cos(yaw) * scale and sin(yaw) * scale, filled in by PlantLayer
};

struct Footprint {
    double x0, x1, z0, z1;
};

struct Site {
    double x, z, y, yaw;
    int variant;
    Footprint footprint;
    double plateau;
};

enum CellKind { kCellHouses = 0, kCellBushes = 1, kCellTrees = 2, kCellGrass = 3 };

class World {
public:
    static constexpr double kExtent = 3200;      // terrain half-size in metres
    static constexpr double kSiteSpacing = 48;   // house lattice spacing
    static constexpr double kHeroClear = 70;     // no town houses this close to the hero house
    static constexpr double kTownRadius = 2700;
    static constexpr int kHouseVariants = 4;     // town variants (the hero house is variant 0)
    static constexpr int kTowerTypes = 3;        // TowerA, TowerB, TowerC; their variants follow the house variants

    // Variant counts of the scattered layers, set to match the meshes the scene grew.
    int bush_variants = 10;
    int grass_variants = 8;
    int tree_round = 9;   // round-tree variants come first
    int oak_sizes = 3;    // then oaks: oak_sizes x oak_forms (broad, medium, tall)
    int oak_forms = 3;

    World();

    double raw_height(double x, double z) const;
    double terrain_height(double x, double z) const;
    // Distance to the nearest house centre, and whether (x, z) is inside a footprint widened by margin.
    void house_proximity(double x, double z, double margin, double& nearest, bool& inside) const;

    void generate_cell(int kind, int cx, int cz, double cell_size, std::vector<Instance>& out) const;

    const Site& hero() const { return hero_; }
    const std::vector<Site>& showcase_towers() const { return towers_; }

private:
    Site hero_;
    std::vector<Site> towers_;
    struct SiteSlot {
        bool has = false;
        Site site;
    };
    // Filled lazily by every layer's cell generation, which runs on worker threads (PlantLayer::update_layers).
    // Entries never move (unordered_map nodes are stable), so a returned Site stays valid after the lock.
    mutable std::unordered_map<int64_t, SiteSlot> site_cache_;
    mutable std::mutex site_mutex_;

    const Site* lattice_site(int i, int j) const;
    template <typename Fn>
    void sites_around(double x, double z, Fn&& fn) const;

    struct TreeSpot {
        int k;
        double x, z;
        bool oak;
    };
    void tree_spots(int cx, int cz, double cell, std::vector<TreeSpot>& out) const;

    void house_cell(int cx, int cz, std::vector<Instance>& out) const;
    void bush_cell(int cx, int cz, double cell, std::vector<Instance>& out) const;
    void tree_cell(int cx, int cz, double cell, std::vector<Instance>& out) const;
    void grass_cell(int cx, int cz, double cell, std::vector<Instance>& out) const;
};

double hash2(int64_t x, int64_t z, int64_t salt);
double fbm(double x, double z, int octaves);

}  // namespace overgrowth

namespace godot {

class DemoWorld : public RefCounted {
    GDCLASS(DemoWorld, RefCounted)

public:
    overgrowth::World& world() { return world_; }

    void configure_variants(int bushes, int grass, int round_trees, int oak_sizes, int oak_forms);
    float terrain_height(float x, float z) const;
    Array get_cell(int kind, int cx, int cz, float cell_size) const;
    Array build_terrain_chunks(int resolution, int chunks) const;
    Ref<ArrayMesh> build_mountain_mesh() const;

protected:
    static void _bind_methods();

private:
    overgrowth::World world_;
};

}  // namespace godot
