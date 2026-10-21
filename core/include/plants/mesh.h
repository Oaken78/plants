// Mesh output: one compact vertex stream per LOD.
//
// Vertex layout (structure of arrays, 24 bytes per vertex):
//   position  float32 x3
//   normal    int8 x4    snorm xyz (smooth shading normal), w unused
//   shade     uint8 x4   unorm: hue shift (0.5 = none), value (x + 0.5), ambient occlusion, edge
//                        edge: 0 = leaf midrib, 1 = leaf outline, 0.5 = no ink line
//   aux       uint8 x4   unorm: wind flex (x 0.25 m), wind phase, birth (growth time), material id (raw)
// The same layout maps to a Godot ArrayMesh: VERTEX, NORMAL, CUSTOM0 (RGBA8 unorm), CUSTOM1 (RGBA8 unorm).
#pragma once

#include <vector>
#include "params.h"
#include "plant_set.h"

namespace plants {

constexpr int kLodCount = 4;
constexpr float kFlexScale = 0.25f;

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
    uint32_t addVertex(const Vec3& p, const Vec3& n, float hue, float value, float ao, float edge, float flex,
                       float phase, float birth, uint8_t mat);
    void addTriangle(uint32_t a, uint32_t b, uint32_t c) { indices.push_back(a); indices.push_back(b); indices.push_back(c); }
};

// LOD 0: every leaf and stem. LOD 1: thinned, enlarged leaves and thick stems.
// LOD 2: clump puffs plus a few large silhouette leaves. LOD 3: merged low-poly puffs only.
void buildLod(const PlantSet& set, const Params& p, int lod, MeshBuffers& out);

}  // namespace plants
