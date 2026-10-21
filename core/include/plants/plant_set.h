// The generator's intermediate representation: stems, leaves, flowers and clumps.
// Growth algorithms fill a PlantSet; the mesher turns one PlantSet into every LOD.
#pragma once

#include <vector>
#include "math.h"

namespace plants {

// Material ids shared with the shaders (web/js/materials.js and the future Godot shaders).
enum Material : uint8_t {
    kMatStem = 0,
    kMatIvyLeaf = 1,
    kMatBushLeaf = 2,
    kMatGrass = 3,
    kMatFlowerWhite = 4,
    kMatFlowerYellow = 5,
    kMatFlowerCenter = 6,
    kMatTreeLeaf = 7,
    kMatBark = 8,
    kMatOakLeaf = 9,
};

enum LeafShape : uint8_t { kShapeIvy = 0, kShapeBush = 1, kShapePetal = 2, kShapeOak = 3 };

struct StemNode {
    Vec3 pos;
    Vec3 normal;        // surface normal (or outward direction for hanging strands)
    float radius = 0;
    float birth = 0;    // 0..1, used by the growth animation
    float flex = 0;     // wind displacement in metres
    int parent = -1;
};

struct Stem {
    std::vector<int> nodes;  // polyline through StemNode indices (first may be the parent's node)
    uint8_t mat = kMatStem;  // kMatStem for soft stems, kMatBark for tree trunks, limbs and roots
};

struct Leaf {
    Vec3 pos;           // attachment point (leaf base)
    Vec3 normal;        // geometric leaf normal
    Vec3 forward;       // base-to-tip direction
    Vec3 shadeNormal;   // smooth normal used for lighting (filled by finalizeShading)
    float size = 0.1f;
    float birth = 0;
    float flex = 0;
    float phase = 0;
    float hue = 0;      // -1..1 hue shift
    float value = 1;    // brightness multiplier, ~0.7..1.3
    float ao = 1;       // 0 = fully occluded
    float keep = 0;     // 0..1 random rank used to thin leaves for lower LODs
    int group = 0;      // plant id: shading only blends with clumps of the same group
    uint8_t mat = kMatIvyLeaf;
    uint8_t shape = kShapeIvy;
};

struct Flower {
    Vec3 pos, normal, shadeNormal;
    float size = 0.05f;
    float birth = 0, flex = 0, phase = 0, keep = 0;
    int group = 0;
    uint8_t mat = kMatFlowerWhite;
};

// A soft volume of foliage. Clumps drive the smooth "cloud" shading of leaves and become the
// low-poly puffs that replace individual leaves at distance.
struct Clump {
    Vec3 center;
    Vec3 radii;         // x/z across the surface, y along axis
    Vec3 axis{0, 1, 0}; // the clump's "up": surface normal for ivy, growth up for bushes
    int group = 0;
    uint8_t mat = kMatBushLeaf;
    float hue = 0, value = 1, flex = 0.02f, phase = 0;
};

// Grass blades are kept as explicit strips (their own shading rules).
struct Blade {
    Vec3 base, dir, side;
    float height = 0.3f, width = 0.03f, bend = 0.3f;
    float hue = 0, value = 1, phase = 0, keep = 0;
};

struct PlantSet {
    std::vector<StemNode> nodes;
    std::vector<Stem> stems;
    std::vector<Leaf> leaves;
    std::vector<Flower> flowers;
    std::vector<Clump> clumps;
    std::vector<Blade> blades;
    int climberCount = 0;
    int bushCount = 0;
    int treeCount = 0;
    int nextGroup = 1;  // foliage group ids for bushes and trees; group 0 is the climbers
};

}  // namespace plants
