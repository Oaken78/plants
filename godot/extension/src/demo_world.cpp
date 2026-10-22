#include "demo_world.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/core/class_db.hpp>

namespace overgrowth {

namespace {

constexpr double kTau = 6.283185307179586;

double smoothstep(double e0, double e1, double x) {
    double t = std::min(1.0, std::max(0.0, (x - e0) / (e1 - e0)));
    return t * t * (3 - 2 * t);
}

double vnoise(double x, double z) {
    const double ix = std::floor(x), iz = std::floor(z);
    double fx = x - ix, fz = z - iz;
    fx = fx * fx * (3 - 2 * fx);
    fz = fz * fz * (3 - 2 * fz);
    const int64_t i = int64_t(ix), k = int64_t(iz);
    const double a = hash2(i, k, 9), b = hash2(i + 1, k, 9), c = hash2(i, k + 1, 9), d = hash2(i + 1, k + 1, 9);
    return a + (b - a) * fx + (c - a) * fz + (a - b - c + d) * fx * fz;
}

// Footprints in local space, metres: the house (main block, annex, balcony) and each tower (with balconies).
constexpr Footprint kHouseFootprint{-7.7, 4.2, -3.2, 4.3};
constexpr Footprint kTowerFootprints[3] = {
    {-5.1, 5.1, -5.1, 6.1},
    {-4.1, 4.1, -6.1, 7.1},
    {-7.1, 7.1, -4.6, 5.6},
};

int tower_variant(int type) { return 1 + World::kHouseVariants + type; }

}  // namespace

// JavaScript: (Math.imul(x | 0, 374761393) + Math.imul(z | 0, 668265263) + Math.imul(salt | 0, 1442695041)) | 0, then
// one xorshift-multiply round. Unsigned 32-bit arithmetic gives the same bits.
double hash2(int64_t x, int64_t z, int64_t salt) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(z) * 668265263u + uint32_t(salt) * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return double(h) / 4294967296.0;
}

double fbm(double x, double z, int octaves) {
    double s = 0, a = 0.5;
    for (int i = 0; i < octaves; ++i) {
        s += a * vnoise(x, z);
        x = x * 2.03 + 17.3;
        z = z * 2.03 - 9.1;
        a *= 0.5;
    }
    return s;
}

World::World() {
    hero_ = Site{0, 0, 0, 0, 0, kHouseFootprint, 19};
    // Three towers just outside the hero house's clearing, so they are in view from the start.
    struct T {
        double x, z, yaw;
        int type;
    };
    const T towers[3] = {{-46, -42, 0.35, 0}, {38, -58, -0.25, 1}, {62, 28, 1.2, 2}};
    for (const T& t : towers)
        towers_.push_back(Site{t.x, t.z, raw_height(t.x, t.z), t.yaw, tower_variant(t.type), kTowerFootprints[t.type], 24});
}

double World::raw_height(double x, double z) const {
    const double r = std::hypot(x, z);
    const double hills = fbm(x * 0.0017 + 3.1, z * 0.0017 - 1.7, 4) * 52 - 22;
    const double detail = fbm(x * 0.011 + 31, z * 0.011 - 7, 3) * 4 - 2;
    return (hills + detail) * smoothstep(45, 230, r);
}

// House site on lattice cell (i, j), or null. Sites cluster into villages.
const Site* World::lattice_site(int i, int j) const {
    std::lock_guard<std::mutex> lock(site_mutex_);
    const int64_t key = (int64_t(i) << 32) ^ int64_t(uint32_t(j));
    auto it = site_cache_.find(key);
    if (it != site_cache_.end()) return it->second.has ? &it->second.site : nullptr;
    SiteSlot slot;
    const double s = kSiteSpacing;
    const double x = (i + 0.5) * s + (hash2(i, j, 1) - 0.5) * 16;
    const double z = (j + 0.5) * s + (hash2(i, j, 2) - 0.5) * 16;
    const double r = std::hypot(x, z);
    if (r > kHeroClear && r < kTownRadius) {
        const double village = fbm(x * 0.0042 + 50, z * 0.0042, 3);
        const double density = smoothstep(0.34, 0.58, village) * 0.9 + 0.08;
        if (hash2(i, j, 3) < density) {
            // Village cores mix in tall towers.
            const bool tower = village > 0.55 && hash2(i, j, 6) < 0.4;
            const int type = int(std::floor(hash2(i, j, 7) * kTowerTypes));
            slot.has = true;
            slot.site = Site{x, z, raw_height(x, z), hash2(i, j, 4) * kTau,
                             tower ? tower_variant(type) : 1 + int(std::floor(hash2(i, j, 5) * kHouseVariants)),
                             tower ? kTowerFootprints[type] : kHouseFootprint, tower ? 24.0 : 19.0};
        }
    }
    auto inserted = site_cache_.emplace(key, slot).first;
    return inserted->second.has ? &inserted->second.site : nullptr;
}

template <typename Fn>
void World::sites_around(double x, double z, Fn&& fn) const {
    fn(hero_);
    for (const Site& t : towers_) fn(t);
    const int i0 = int(std::floor(x / kSiteSpacing)), j0 = int(std::floor(z / kSiteSpacing));
    for (int j = j0 - 1; j <= j0 + 1; ++j)
        for (int i = i0 - 1; i <= i0 + 1; ++i)
            if (const Site* site = lattice_site(i, j)) fn(*site);
}

// Terrain height with flat plateaus under every house.
double World::terrain_height(double x, double z) const {
    double h = raw_height(x, z);
    sites_around(x, z, [&](const Site& site) {
        const double d = std::hypot(x - site.x, z - site.z);
        const double w = 1 - smoothstep(site.plateau - 8, site.plateau, d);
        if (w > 0) h += (site.y - h) * w;
    });
    return h;
}

void World::house_proximity(double x, double z, double margin, double& nearest, bool& inside) const {
    nearest = 1e300;
    inside = false;
    sites_around(x, z, [&](const Site& site) {
        const double dx = x - site.x, dz = z - site.z;
        nearest = std::min(nearest, std::hypot(dx, dz));
        const double c = std::cos(site.yaw), s = std::sin(site.yaw);
        const double lx = c * dx - s * dz, lz = s * dx + c * dz;  // inverse of the instance rotation
        const Footprint& f = site.footprint;
        if (lx > f.x0 - margin && lx < f.x1 + margin && lz > f.z0 - margin && lz < f.z1 + margin) inside = true;
    });
}

void World::generate_cell(int kind, int cx, int cz, double cell_size, std::vector<Instance>& out) const {
    switch (kind) {
        case kCellHouses: house_cell(cx, cz, out); break;
        case kCellBushes: bush_cell(cx, cz, cell_size, out); break;
        case kCellTrees: tree_cell(cx, cz, cell_size, out); break;
        case kCellGrass: grass_cell(cx, cz, cell_size, out); break;
        default: break;
    }
}

void World::house_cell(int cx, int cz, std::vector<Instance>& out) const {
    auto push = [&](const Site& s) { out.push_back({float(s.x), float(s.y), float(s.z), float(s.yaw), 1.0f, s.variant}); };
    if (cx == 0 && cz == 0) push(hero_);
    const double s = kSiteSpacing;
    for (const Site& t : towers_)
        if (int(std::floor(t.x / s)) == cx && int(std::floor(t.z / s)) == cz) push(t);
    // House layer cells use the same spacing as the site lattice, so cell (i, j) owns site (i, j).
    if (const Site* site = lattice_site(cx, cz)) push(*site);
}

void World::bush_cell(int cx, int cz, double cell, std::vector<Instance>& out) const {
    const double x0 = cx * cell, z0 = cz * cell;
    const double meadow = fbm((x0 + cell / 2) * 0.009 + 5, (z0 + cell / 2) * 0.009, 3);
    const int count = int(std::floor(hash2(cx, cz, 11) * 14 * smoothstep(0.3, 0.6, meadow))) + (hash2(cx, cz, 12) < 0.45 ? 1 : 0);
    for (int k = 0; k < count; ++k) {
        const double x = x0 + hash2(int64_t(cx) * 31 + k, cz, 13) * cell;
        const double z = z0 + hash2(cx, int64_t(cz) * 31 + k, 14) * cell;
        double nearest;
        bool inside;
        house_proximity(x, z, 0, nearest, inside);
        if (nearest < 12) continue;  // houses grow their own bushes
        out.push_back({float(x), float(terrain_height(x, z) - 0.05), float(z), float(hash2(cx + k, cz - k, 15) * kTau),
                       float(0.55 + std::pow(hash2(cx - k, cz + k, 16), 2) * 1.9),
                       int(std::floor(hash2(int64_t(cx) + 7 * k, cz, 17) * bush_variants))});
    }
}

// Candidate tree spots of one cell: groves, plus lone oaks out in the open meadows. Pure function of the cell, so
// neighbouring cells can be inspected without generating them.
void World::tree_spots(int cx, int cz, double cell, std::vector<TreeSpot>& out) const {
    const double x0 = cx * cell, z0 = cz * cell;
    const double grove = fbm((x0 + cell / 2) * 0.006 - 13, (z0 + cell / 2) * 0.006 + 4, 3);
    const int count = int(std::floor(hash2(cx, cz, 31) * 7 * smoothstep(0.45, 0.68, grove))) + (hash2(cx, cz, 32) < 0.25 ? 1 : 0);
    for (int k = 0; k < count; ++k) {
        out.push_back({k, x0 + hash2(int64_t(cx) * 53 + k, cz, 33) * cell, z0 + hash2(cx, int64_t(cz) * 53 + k, 34) * cell,
                       hash2(int64_t(cx) - 7 * k, cz + k, 38) < 0.3});
    }
    if (grove < 0.45 && hash2(cx, cz, 39) < 0.12)
        out.push_back({99, x0 + hash2(cx, cz, 40) * cell, z0 + hash2(cx, cz, 41) * cell, true});
}

// Trees gather in groves and line the edges of villages. An oak's form follows how crowded its spot is by other
// trees and by houses, like a real oak competing for light.
void World::tree_cell(int cx, int cz, double cell, std::vector<Instance>& out) const {
    std::vector<TreeSpot> near, own;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) tree_spots(cx + dx, cz + dz, cell, near);
    tree_spots(cx, cz, cell, own);
    for (const TreeSpot& s : own) {
        double house;
        bool inside;
        house_proximity(s.x, s.z, 0, house, inside);
        if (house < 15) continue;                  // houses grow their own trees
        if (std::hypot(s.x, s.z) < 24) continue;   // keep the hero house in the open
        int variant;
        if (s.oak) {
            double trees = 0;
            for (const TreeSpot& o : near) {
                const double d = std::hypot(o.x - s.x, o.z - s.z);
                if (d > 0.01 && d < 14) trees += 1 - d / 14;
            }
            const double crowding = std::min(1.0, trees * 0.5 + std::max(0.0, (30 - house) / 30) * 0.9);
            const int form = std::min(oak_forms - 1, int(std::floor(crowding * oak_forms)));
            const int size = int(std::floor(hash2(int64_t(cx) + s.k, int64_t(cz) - 3 * s.k, 42) * oak_sizes));
            variant = tree_round + size * oak_forms + form;
        } else {
            variant = int(std::floor(hash2(int64_t(cx) + 11 * s.k, cz, 37) * tree_round));
        }
        out.push_back({float(s.x), float(terrain_height(s.x, s.z) - 0.1), float(s.z),
                       float(hash2(int64_t(cx) + 3 * s.k, int64_t(cz) - s.k, 35) * kTau),
                       float(0.85 + hash2(int64_t(cx) - s.k, int64_t(cz) + 2 * s.k, 36) * 0.35), variant});
    }
}

void World::grass_cell(int cx, int cz, double cell, std::vector<Instance>& out) const {
    const double x0 = cx * cell, z0 = cz * cell;
    for (int k = 0; k < 40; ++k) {
        const double x = x0 + hash2(int64_t(cx) * 97 + k, cz, 21) * cell;
        const double z = z0 + hash2(cx, int64_t(cz) * 97 + k, 22) * cell;
        const double meadow = fbm(x * 0.05, z * 0.05, 2);
        if (hash2(cx + k, int64_t(cz) * 3 + k, 23) > smoothstep(0.25, 0.6, meadow) * 0.9 + 0.1) continue;
        double nearest;
        bool inside;
        house_proximity(x, z, 0.4, nearest, inside);
        if (inside) continue;
        out.push_back({float(x), float(terrain_height(x, z) - 0.02), float(z), float(hash2(cx - k, int64_t(cz) + 5 * k, 24) * kTau),
                       float(0.7 + hash2(int64_t(cx) + 3 * k, cz - k, 25) * 0.7),
                       int(std::floor(hash2(int64_t(cx) * 5 + k, cz, 26) * grass_variants))});
    }
}

}  // namespace overgrowth

namespace godot {

namespace {

// Vertex normals as three.js computeVertexNormals makes them: area-weighted face normals, summed and normalised.
// Triangles are wound clockwise (Godot's front face) where three.js winds them counter-clockwise; with the
// operands in this order the face normal still points out of the front.
PackedVector3Array vertex_normals(const PackedVector3Array& pos, const PackedInt32Array& idx) {
    PackedVector3Array nrm;
    nrm.resize(pos.size());
    Vector3* n = nrm.ptrw();
    const Vector3* p = pos.ptr();
    const int32_t* t = idx.ptr();
    for (int64_t i = 0; i < idx.size(); i += 3) {
        const Vector3 face = (p[t[i + 1]] - p[t[i + 2]]).cross(p[t[i]] - p[t[i + 2]]);
        n[t[i]] += face;
        n[t[i + 1]] += face;
        n[t[i + 2]] += face;
    }
    for (int64_t i = 0; i < nrm.size(); ++i) n[i] = n[i].normalized();
    return nrm;
}

Ref<ArrayMesh> make_mesh(const PackedVector3Array& pos, const PackedVector3Array& nrm, const PackedInt32Array& idx) {
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = pos;
    arrays[Mesh::ARRAY_NORMAL] = nrm;
    arrays[Mesh::ARRAY_INDEX] = idx;
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    return mesh;
}

}  // namespace

void DemoWorld::configure_variants(int bushes, int grass, int round_trees, int oak_sizes, int oak_forms) {
    world_.bush_variants = bushes;
    world_.grass_variants = grass;
    world_.tree_round = round_trees;
    world_.oak_sizes = oak_sizes;
    world_.oak_forms = oak_forms;
}

float DemoWorld::terrain_height(float x, float z) const { return float(world_.terrain_height(x, z)); }

Array DemoWorld::get_cell(int kind, int cx, int cz, float cell_size) const {
    std::vector<overgrowth::Instance> list;
    world_.generate_cell(kind, cx, cz, cell_size, list);
    Array out;
    for (const auto& i : list) {
        Dictionary d;
        d["position"] = Vector3(i.x, i.y, i.z);
        d["yaw"] = i.yaw;
        d["scale"] = i.scale;
        d["variant"] = i.variant;
        out.push_back(d);
    }
    return out;
}

// Terrain grid with spacing ~1.3 m at the centre growing to ~40 m at the rim, cut into chunks x chunks meshes. As one
// mesh it would be drawn whole into every shadow cascade and behind the camera; as chunks Godot culls each one, and
// since the grid is warped the chunks are small near the centre and large at the rim. Normals come from the whole
// grid, so the seams do not show.
Array DemoWorld::build_terrain_chunks(int n, int chunks) const {
    const double e = overgrowth::World::kExtent;
    auto warp = [e](double u) { return e * (0.08 * u + 0.92 * u * u * u); };
    const int verts = n + 1;
    PackedVector3Array pos;
    pos.resize(int64_t(verts) * verts);
    Vector3* p = pos.ptrw();
    for (int j = 0; j <= n; ++j) {
        const double z = warp(double(j) / n * 2 - 1);
        for (int i = 0; i <= n; ++i) {
            const double x = warp(double(i) / n * 2 - 1);
            p[j * verts + i] = Vector3(float(x), float(world_.terrain_height(x, z)), float(z));
        }
    }
    PackedInt32Array idx;
    idx.resize(int64_t(n) * n * 6);
    int32_t* t = idx.ptrw();
    for (int j = 0, k = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i, k += 6) {
            const int a = j * verts + i, b = a + 1, c = a + verts, d = c + 1;
            t[k] = a; t[k + 1] = b; t[k + 2] = c;
            t[k + 3] = b; t[k + 4] = d; t[k + 5] = c;
        }
    }
    const PackedVector3Array nrm = vertex_normals(pos, idx);
    Array out;
    for (int cj = 0; cj < chunks; ++cj) {
        for (int ci = 0; ci < chunks; ++ci) {
            const int i0 = ci * n / chunks, i1 = (ci + 1) * n / chunks;
            const int j0 = cj * n / chunks, j1 = (cj + 1) * n / chunks;
            const int w = i1 - i0 + 1;
            PackedVector3Array cp, cn;
            PackedInt32Array ct;
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i) {
                    cp.push_back(pos[j * verts + i]);
                    cn.push_back(nrm[j * verts + i]);
                }
            for (int j = 0; j < j1 - j0; ++j)
                for (int i = 0; i < i1 - i0; ++i) {
                    const int a = j * w + i, b = a + 1, c = a + w, d = c + 1;
                    const int tri[6] = {a, b, c, b, d, c};
                    for (int v : tri) ct.push_back(v);
                }
            out.push_back(make_mesh(cp, cn, ct));
        }
    }
    return out;
}

// A ring of distant blue mountains beyond the terrain.
Ref<ArrayMesh> DemoWorld::build_mountain_mesh() const {
    using overgrowth::fbm;
    const int seg = 320;
    auto ring_height = [](int ring, double a) {
        switch (ring) {
            case 1: return 180 + fbm(a * 3.0, 1.3, 4) * 520 + std::max(0.0, fbm(a * 9, 4.2, 2) - 0.5) * 300;
            case 2: return 120 + fbm(a * 2.0, 7.9, 3) * 380;
            default: return -40.0;
        }
    };
    const double radii[4] = {3300, 4300, 5600, 7000};
    PackedVector3Array pos;
    for (int r = 0; r < 4; ++r) {
        for (int s = 0; s <= seg; ++s) {
            const double a = double(s) / seg * overgrowth::kTau;
            pos.push_back(Vector3(float(std::cos(a) * radii[r]), float(ring_height(r, std::fmod(a, overgrowth::kTau))),
                                  float(std::sin(a) * radii[r])));
        }
    }
    PackedInt32Array idx;
    for (int r = 0; r < 3; ++r) {
        for (int s = 0; s < seg; ++s) {
            const int a = r * (seg + 1) + s, b = a + 1, c = a + seg + 1, d = c + 1;
            const int tri[6] = {a, c, b, b, c, d};
            for (int v : tri) idx.push_back(v);
        }
    }
    return make_mesh(pos, vertex_normals(pos, idx), idx);
}

void DemoWorld::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure_variants", "bushes", "grass", "round_trees", "oak_sizes", "oak_forms"),
                         &DemoWorld::configure_variants);
    ClassDB::bind_method(D_METHOD("terrain_height", "x", "z"), &DemoWorld::terrain_height);
    ClassDB::bind_method(D_METHOD("get_cell", "kind", "cx", "cz", "cell_size"), &DemoWorld::get_cell);
    ClassDB::bind_method(D_METHOD("build_terrain_chunks", "resolution", "chunks"), &DemoWorld::build_terrain_chunks,
                         DEFVAL(420), DEFVAL(12));
    ClassDB::bind_method(D_METHOD("build_mountain_mesh"), &DemoWorld::build_mountain_mesh);
}

}  // namespace godot
