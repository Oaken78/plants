// Mesh output: one compact vertex stream per LOD.
//
// Vertex layout (structure of arrays, 24 bytes per vertex):
//   position  float32 x3
//   normal    int8 x4    snorm xyz (smooth shading normal), w = leaf u (across the leaf, x 0.6)
//   shade     uint8 x4   unorm: hue shift (0.5 = none), value (x + 0.5), ambient occlusion, leaf v
//                        leaf v: 0..250 maps to -0.1..1.1 along the leaf (base to tip); 255 = not a leaf.
//                        Leaves are coarse hulls; the shader cuts the true smooth outline from (u, v), see
//                        leafHalfWidth() below and in web/js/materials.js.
//   aux       uint8 x4   unorm: wind flex (x 0.25 m), wind phase, birth (growth time), material id (raw)
// The same layout maps to a Godot ArrayMesh: VERTEX, NORMAL, CUSTOM0 (RGBA8 unorm), CUSTOM1 (RGBA8 unorm).
#pragma once

#include <vector>
#include "params.h"
#include "plant_set.h"

namespace plants {

constexpr int kLodCount = 4;
constexpr float kFlexScale = 0.25f;
constexpr float kNotLeaf = -9.0f;

// Half width of a leaf shape at y (0 = base, 1 = tip), in units of the leaf's length.
// Must match leafHalfWidth() in web/js/materials.js, which cuts the outline per pixel.
float leafHalfWidth(int shape, float y);

struct MeshBuffers {
    std::vector<float> positions;
    std::vector<int8_t> normals;
    std::vector<uint8_t> shade;
    std::vector<uint8_t> aux;
    std::vector<uint32_t> indices;
    Aabb bounds;

    int vertexCount() const { return int(positions.size() / 3); }
    void clear() {
        positions.clear(); normals.clear(); shade.clear(); aux.clear(); indices.clear();
        bounds = Aabb{};
    }
    // leafU/leafV: the vertex's position in leaf space (x across, y base to tip); pass kNotLeaf as leafV otherwise.
    uint32_t addVertex(const Vec3& p, const Vec3& n, float hue, float value, float ao, float leafU, float leafV, float flex,
                       float phase, float birth, uint8_t mat);
    void addTriangle(uint32_t a, uint32_t b, uint32_t c) { indices.push_back(a); indices.push_back(b); indices.push_back(c); }
};

// LOD 0: every leaf and stem. LOD 1: thinned, enlarged leaves and thick stems.
// LOD 2: clump puffs plus a few large silhouette leaves. LOD 3: merged low-poly puffs only.
void buildLod(const PlantSet& set, const Params& p, int lod, MeshBuffers& out);

// A coarser variant of the last LOD for far away: clumps merged per mergeCell metres, and stems thinner than
// minStemRadius dropped (LOD 3 itself uses lod3.mergeCell and 0.07 m).
void buildFarLod(const PlantSet& set, const Params& p, float mergeCell, float minStemRadius, MeshBuffers& out);

}  // namespace plants
