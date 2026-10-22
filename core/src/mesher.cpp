// Turns a PlantSet into renderable meshes, one per LOD.
#include <map>
#include <tuple>
#include "plants/generator.h"
#include "plants/mesh.h"

namespace plants {

uint32_t MeshBuffers::addVertex(const Vec3& p, const Vec3& n, float hue, float value, float ao, float leafU, float leafV,
                                float flex, float phase, float birth, uint8_t mat) {
    auto u8 = [](float v) { return uint8_t(saturate(v) * 255.0f + 0.5f); };
    auto s8 = [](float v) { return int8_t(std::lround(clampf(v, -1.0f, 1.0f) * 127.0f)); };
    const bool leaf = leafV > kNotLeaf * 0.5f;
    uint32_t index = uint32_t(positions.size() / 3);
    positions.push_back(p.x);
    positions.push_back(p.y);
    positions.push_back(p.z);
    bounds.grow(p);
    Vec3 nn = normalize(n);
    normals.push_back(s8(nn.x));
    normals.push_back(s8(nn.y));
    normals.push_back(s8(nn.z));
    normals.push_back(leaf ? s8(leafU / 0.6f) : 0);
    shade.push_back(u8(hue * 0.5f + 0.5f));
    shade.push_back(u8(value - 0.5f));
    shade.push_back(u8(ao));
    shade.push_back(leaf ? uint8_t(saturate((leafV + 0.1f) / 1.2f) * 250.0f + 0.5f) : uint8_t(255));
    aux.push_back(u8(flex / kFlexScale));
    aux.push_back(u8(phase - std::floor(phase)));
    aux.push_back(u8(birth));
    aux.push_back(mat);
    return index;
}

static float gauss(float y, float m, float s) {
    float t = (y - m) / s;
    return std::exp(-t * t);
}

float leafHalfWidth(int shape, float y) {
    float yc = clampf(y, 0.0f, 1.0f);
    switch (shape) {
        case kShapeIvy:  // basal, side and tip lobes of a hedera leaf
            return 0.40f * gauss(y, 0.12f, 0.15f) + 0.48f * gauss(y, 0.42f, 0.16f) + 0.22f * gauss(y, 0.72f, 0.16f);
        case kShapeOak: {
            // Rounded at base and tip, widest past the middle, four broad lobes a side with deep narrow sinuses.
            float env = 0.42f * std::sqrt(std::max(std::sin(kPi * std::pow(yc, 1.25f)), 0.0f));
            float sinus = 0.5f - 0.5f * std::cos(2 * kPi * 4.0f * yc);
            return env * (1.0f - 0.5f * sinus * sinus * sinus);
        }
        case kShapePetal: return 0.30f * std::pow(std::max(std::sin(kPi * yc), 0.0f), 0.55f);
        default:  // bush and round-tree leaves: a pointed ellipse
            return 0.30f * std::pow(std::max(std::sin(kPi * yc), 0.0f), 0.75f) * (1.0f - 0.25f * yc);
    }
}

namespace {

const Vec3 kUp{0, 1, 0};

struct LeafTemplate {
    struct Vert {
        float x, y;
    };
    std::vector<Vert> verts;
    std::vector<uint32_t> tris;
};

// A leaf is a ribbon of rows (left edge, midrib, right edge) wide enough to hold the true shape between every pair
// of rows; the shader discards what lies outside the shape. Fewer rows only bend the leaf less, the outline stays
// exactly as smooth.
LeafTemplate makeHull(int shape, const std::vector<float>& rows) {
    LeafTemplate t;
    for (size_t r = 0; r < rows.size(); ++r) {
        float y0 = rows[r > 0 ? r - 1 : r], y1 = rows[r + 1 < rows.size() ? r + 1 : r];
        float w = 0;
        for (int k = 0; k <= 32; ++k) w = std::max(w, leafHalfWidth(shape, lerpf(y0, y1, k / 32.0f)));
        w += 0.03f;
        t.verts.push_back({-w, rows[r]});
        t.verts.push_back({0.0f, rows[r]});
        t.verts.push_back({w, rows[r]});
    }
    for (uint32_t r = 0; r + 1 < rows.size(); ++r) {
        uint32_t l0 = r * 3, c0 = l0 + 1, r0 = l0 + 2, l1 = l0 + 3, c1 = l0 + 4, r1 = l0 + 5;
        uint32_t tris[12] = {c0, r0, r1, c0, r1, c1, c0, c1, l1, c0, l1, l0};
        t.tris.insert(t.tris.end(), tris, tris + 12);
    }
    return t;
}

const LeafTemplate& leafTemplate(int shape, bool simple) {
    static const std::vector<float> full{-0.08f, 0.3f, 0.65f, 1.02f}, coarse{-0.08f, 1.02f};
    static const LeafTemplate ivy = makeHull(kShapeIvy, full), ivySimple = makeHull(kShapeIvy, coarse);
    static const LeafTemplate bush = makeHull(kShapeBush, full), bushSimple = makeHull(kShapeBush, coarse);
    static const LeafTemplate oak = makeHull(kShapeOak, full), oakSimple = makeHull(kShapeOak, coarse);
    static const LeafTemplate petal = makeHull(kShapePetal, {-0.02f, 1.02f});
    switch (shape) {
        case kShapeIvy: return simple ? ivySimple : ivy;
        case kShapeBush: return simple ? bushSimple : bush;
        case kShapeOak: return simple ? oakSimple : oak;
        default: return petal;
    }
}

struct Icosphere {
    std::vector<Vec3> verts;
    std::vector<uint32_t> tris;
};

Icosphere makeIcosphere(int level) {
    Icosphere s;
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    Vec3 base[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                     {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& v : base) s.verts.push_back(normalize(v));
    s.tris = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
              3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (int l = 0; l < level; ++l) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
        auto midpoint = [&](uint32_t a, uint32_t b) {
            auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            s.verts.push_back(normalize(s.verts[a] + s.verts[b]));
            uint32_t idx = uint32_t(s.verts.size() - 1);
            mid[key] = idx;
            return idx;
        };
        std::vector<uint32_t> next;
        for (size_t i = 0; i < s.tris.size(); i += 3) {
            uint32_t a = s.tris[i], b = s.tris[i + 1], c = s.tris[i + 2];
            uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            uint32_t tri[12] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
            next.insert(next.end(), tri, tri + 12);
        }
        s.tris.swap(next);
    }
    return s;
}

const Icosphere& icosphere(int level) {
    static const Icosphere l0 = makeIcosphere(0);
    static const Icosphere l1 = makeIcosphere(1);
    return level <= 0 ? l0 : l1;
}

struct LodConfig {
    float leafKeep, leafScale;
    bool simpleLeaves;
    int tubeSides, tubeStride;
    float minStemRadius;
    float flowerKeep, flowerScale;
    bool simpleFlowers;
    float bladeKeep, bladeWidth;
    int puffLevel;    // -1: no puffs
    float mergeCell;  // 0: one puff per clump, else merge clumps per cell of this size
    bool mergeClimbersOnly;
    float puffShrink;
};

LodConfig configFor(int lod, const Params& p) {
    float k1 = p[kParamLod1Keep], k2 = p[kParamLod2Keep];
    switch (lod) {
        case 0: return {1.0f, 1.0f, false, 6, 1, 0.0f, 1.0f, 1.0f, false, 1.0f, 1.0f, -1, 0.0f, false, 0.0f};
        case 1: return {k1, std::min(1.9f, 0.92f / std::sqrt(k1)), true, 4, 2, 0.0045f, 0.5f, 1.3f, false, 0.45f, 1.6f, -1, 0.0f, false, 0.0f};
        // Bush clumps are already chunky at LOD 2; only the many small ivy clumps are merged.
        case 2: return {k2, 1.7f, true, 3, 3, 0.010f, 0.3f, 2.0f, true, 0.0f, 0.0f, 1, p[kParamIvyClumpCell] * 1.7f, true, 0.97f};
        // Only tree trunks are thick enough to keep stems at LOD 3; the crown would float without them.
        default: return {0.0f, 0.0f, true, 3, 4, 0.07f, 0.0f, 0.0f, true, 0.0f, 0.0f, 0, p[kParamLod3MergeCell], false, 1.0f};
    }
}

void emitLeaf(const Leaf& l, const LeafTemplate& t, float scale, MeshBuffers& m) {
    Frame f = makeFrame(l.normal, l.forward);
    float size = l.size * scale;
    Vec3 base = l.pos + f.forward * (size * (l.shape == kShapeIvy ? 0.12f : 0.04f));
    uint32_t first = uint32_t(m.vertexCount());
    for (const auto& v : t.verts) {
        float z = std::fabs(v.x) * 0.35f - v.y * v.y * 0.12f;  // V-fold along the midrib, tip curling back
        Vec3 pos = base + f.side * (v.x * size) + f.forward * (v.y * size) + f.normal * (z * size);
        // Tilt each half's normal a little so leaves near the terminator split into a lit and a shaded half.
        float halfSign = v.x > 0 ? 1.0f : (v.x < 0 ? -1.0f : 0.0f);
        Vec3 n = normalize(l.shadeNormal + f.side * (halfSign * 0.18f), l.shadeNormal);
        m.addVertex(pos, n, l.hue, l.value, l.ao, v.x, v.y, l.flex * (0.6f + 0.4f * v.y), l.phase, l.birth, l.mat);
    }
    for (uint32_t idx : t.tris) m.indices.push_back(first + idx);
}

void emitTube(const PlantSet& s, const std::vector<uint8_t>& hasChild, const Stem& stem, int baseSides, int stride,
              float minRadius, float phase, MeshBuffers& m) {
    std::vector<int> ids;
    for (size_t k = 0; k < stem.nodes.size(); k += stride) ids.push_back(stem.nodes[k]);
    if (ids.back() != stem.nodes.back()) ids.push_back(stem.nodes.back());
    // Cut the stem where it gets thinner than this LOD can show.
    size_t keep = 0;
    while (keep < ids.size() && (keep == 0 || s.nodes[ids[keep]].radius >= minRadius)) ++keep;
    const bool cut = keep < ids.size();
    ids.resize(keep);
    if (ids.size() < 2) return;
    // Only real tips (and stems this LOD cuts short) taper to a point. A stem that ends where others continue,
    // like the trunk at its fork, keeps its full radius so there is no pinch at the junction.
    const bool taper = cut || !hasChild[ids.back()];
    // Thick trunks and roots get twice the sides so they stay round up close.
    const int sides = baseSides * (s.nodes[ids[1]].radius > 0.06f ? 2 : 1);
    const float value = 0.9f + 0.2f * phase;

    Vec3 u = s.nodes[ids[0]].normal;
    uint32_t prevRing = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        const StemNode& node = s.nodes[ids[i]];
        Vec3 a = s.nodes[ids[i == 0 ? 0 : i - 1]].pos;
        Vec3 b = s.nodes[ids[std::min(i + 1, ids.size() - 1)]].pos;
        Vec3 t = normalize(b - a, kUp);
        u = normalize(projectOnPlane(u, t), anyPerpendicular(t));  // parallel transport
        Vec3 v = cross(t, u);
        float r = (taper && i + 1 == ids.size()) ? node.radius * 0.35f : node.radius;
        uint32_t ring = uint32_t(m.vertexCount());
        for (int k = 0; k < sides; ++k) {
            float ang = 2 * kPi * k / sides;
            Vec3 dir = u * std::cos(ang) + v * std::sin(ang);
            m.addVertex(node.pos + dir * r, dir, 0.0f, value, 0.85f, 0.0f, kNotLeaf, node.flex, phase, node.birth, stem.mat);
        }
        if (i > 0) {
            for (int k = 0; k < sides; ++k) {
                uint32_t a0 = prevRing + k, b0 = prevRing + (k + 1) % sides;
                uint32_t c0 = ring + k, d0 = ring + (k + 1) % sides;
                m.addTriangle(a0, b0, c0);
                m.addTriangle(b0, d0, c0);
            }
        }
        prevRing = ring;
    }
}

void emitBlade(const Blade& b, float widthScale, MeshBuffers& m) {
    Vec3 bendDir = normalize(cross(b.side, kUp), b.dir);
    Vec3 n = normalize(kUp + bendDir * 0.25f);
    float flexScale = 0.09f * (b.height / 0.3f);
    uint32_t first = uint32_t(m.vertexCount());
    for (int s = 0; s < 3; ++s) {
        float t = s / 3.0f;
        Vec3 c = b.base + b.dir * (b.height * t) + bendDir * (b.bend * b.height * t * t);
        float w = b.width * widthScale * (1.0f - t * 0.8f) * 0.5f;
        for (int side = -1; side <= 1; side += 2)
            m.addVertex(c + b.side * (w * side), n, b.hue + t * 0.25f, b.value * (0.92f + 0.16f * t), lerpf(0.35f, 1.0f, t),
                        0.0f, kNotLeaf, flexScale * t * t, b.phase, 0.0f, kMatGrass);
    }
    Vec3 tip = b.base + b.dir * b.height + bendDir * (b.bend * b.height);
    m.addVertex(tip, n, b.hue + 0.25f, b.value * 1.08f, 1.0f, 0.0f, kNotLeaf, flexScale, b.phase, 0.0f, kMatGrass);
    for (uint32_t s = 0; s < 2; ++s) {
        uint32_t l0 = first + s * 2, r0 = l0 + 1, l1 = l0 + 2, r1 = l0 + 3;
        m.addTriangle(l0, r0, r1);
        m.addTriangle(l0, r1, l1);
    }
    m.addTriangle(first + 4, first + 5, first + 6);
}

void emitFlower(const Flower& f, float scale, bool simple, MeshBuffers& m) {
    float size = f.size * scale;
    Vec3 n = f.normal;
    Vec3 side0 = anyPerpendicular(n);
    float rot = f.phase * 2 * kPi;
    uint32_t first = uint32_t(m.vertexCount());
    if (simple) {
        m.addVertex(f.pos + n * (size * 0.05f), f.shadeNormal, 0, 1, 1, 0.0f, kNotLeaf, f.flex, f.phase, f.birth, f.mat);
        for (int k = 0; k < 5; ++k) {
            Vec3 d = rotateAxis(side0, n, rot + k * 2 * kPi / 5);
            m.addVertex(f.pos + d * (size * 0.5f), f.shadeNormal, 0, 1, 1, 0.0f, kNotLeaf, f.flex, f.phase, f.birth, f.mat);
        }
        for (uint32_t k = 0; k < 5; ++k) m.addTriangle(first, first + 1 + k, first + 1 + (k + 1) % 5);
        return;
    }
    const LeafTemplate& petal = leafTemplate(kShapePetal, false);
    for (int k = 0; k < 5; ++k) {
        Vec3 fwd = rotateAxis(side0, n, rot + k * 2 * kPi / 5);
        Vec3 side = cross(fwd, n);
        uint32_t base = uint32_t(m.vertexCount());
        for (const auto& v : petal.verts) {
            Vec3 pos = f.pos + fwd * (size * (0.08f + v.y * 0.55f)) + side * (v.x * size * 0.55f) +
                       n * (v.y * v.y * size * 0.18f);
            m.addVertex(pos, f.shadeNormal, 0, 1, 1, v.x, v.y, f.flex, f.phase, f.birth, f.mat);
        }
        for (uint32_t idx : petal.tris) m.indices.push_back(base + idx);
    }
    uint32_t center = uint32_t(m.vertexCount());
    Vec3 c = f.pos + n * (size * 0.05f);
    m.addVertex(c, f.shadeNormal, 0, 1, 1, 0.0f, kNotLeaf, f.flex, f.phase, f.birth, kMatFlowerCenter);
    for (int k = 0; k < 6; ++k) {
        Vec3 d = rotateAxis(side0, n, rot + k * 2 * kPi / 6);
        m.addVertex(c + d * (size * 0.16f), f.shadeNormal, 0, 1, 1, 0.0f, kNotLeaf, f.flex, f.phase, f.birth, kMatFlowerCenter);
    }
    for (uint32_t k = 0; k < 6; ++k) m.addTriangle(center, center + 1 + k, center + 1 + (k + 1) % 6);
}

void emitPuff(const Clump& c, const ClumpField& field, int level, float shrink, MeshBuffers& m) {
    const Icosphere& sphere = icosphere(level);
    Frame f = clumpFrame(c);
    Vec3 offset = c.center * 1.7f + Vec3{c.phase * 13.0f, 0, 0};
    uint32_t first = uint32_t(m.vertexCount());
    for (const Vec3& d : sphere.verts) {
        // Two octaves of lumps give the cauliflower silhouette.
        float lump = (valueNoise(d * 2.2f + offset) - 0.5f) * 0.5f + (valueNoise(d * 4.7f + offset * 1.3f) - 0.5f) * 0.22f;
        Vec3 local = Vec3{d.x * c.radii.x, d.y * c.radii.y, d.z * c.radii.z} * (shrink * (1.0f + lump));
        Vec3 pos = c.center + f.side * local.x + f.normal * local.y + f.forward * local.z;
        Vec3 radial = normalize(f.side * (d.x / c.radii.x) + f.normal * (d.y / c.radii.y) + f.forward * (d.z / c.radii.z));
        FieldSample fs = field.sample(pos, c.group, radial);
        Vec3 n = normalize(lerp(radial, fs.normal, 0.7f), radial);
        float ao = fs.ao * lerpf(0.62f, 1.0f, d.y * 0.5f + 0.5f);
        m.addVertex(pos, n, c.hue, c.value, ao, 0.0f, kNotLeaf, c.flex, c.phase, 0.0f, c.mat);
    }
    for (uint32_t idx : sphere.tris) m.indices.push_back(first + idx);
}

// Merges nearby clumps of the same plant into one bigger puff (all clumps, or only climber clumps).
std::vector<Clump> mergeClumps(const std::vector<Clump>& in, float cell, bool climbersOnly) {
    std::map<std::pair<int, std::tuple<int, int, int>>, std::vector<int>> cells;
    std::vector<std::pair<int, std::tuple<int, int, int>>> order;
    for (size_t i = 0; i < in.size(); ++i) {
        const Vec3& c = in[i].center;
        bool merge = !climbersOnly || in[i].group == 0;
        auto key = std::make_pair(in[i].group, merge ? std::make_tuple(int(std::floor(c.x / cell)), int(std::floor(c.y / cell)),
                                                                       int(std::floor(c.z / cell)))
                                                     : std::make_tuple(int(i), 1 << 30, 0));
        auto& list = cells[key];
        if (list.empty()) order.push_back(key);
        list.push_back(int(i));
    }
    std::vector<Clump> out;
    for (const auto& key : order) {
        const auto& list = cells[key];
        if (list.size() == 1) {
            out.push_back(in[list[0]]);
            continue;
        }
        float volume = 0, ratio = 0, hue = 0, value = 0, flex = 0;
        Vec3 center, axis;
        for (int i : list) {
            const Clump& c = in[i];
            float v = c.radii.x * c.radii.y * c.radii.z;
            volume += v;
            center += c.center * v;
            axis += c.axis * v;
            ratio += (c.radii.y / c.radii.x) * v;
            hue += c.hue * v;
            value += c.value * v;
            flex += c.flex * v;
        }
        center = center / volume;
        float reach = 0;
        for (int i : list) {
            const Clump& c = in[i];
            reach = std::max(reach, length(c.center - center) + std::max(c.radii.x, c.radii.z));
        }
        reach *= 0.82f;
        Clump m = in[list[0]];
        m.center = center;
        m.axis = normalize(axis, in[list[0]].axis);
        ratio /= volume;
        m.radii = {reach, reach * ratio, reach};
        m.hue = hue / volume;
        m.value = value / volume;
        m.flex = flex / volume;
        out.push_back(m);
    }
    return out;
}

void buildWithConfig(const PlantSet& set, const LodConfig& cfg, int lod, MeshBuffers& out) {
    out.clear();
    if (cfg.tubeSides > 0) {
        std::vector<uint8_t> hasChild(set.nodes.size(), 0);
        for (const StemNode& n : set.nodes)
            if (n.parent >= 0) hasChild[n.parent] = 1;
        for (size_t i = 0; i < set.stems.size(); ++i)
            emitTube(set, hasChild, set.stems[i], cfg.tubeSides, cfg.tubeStride, cfg.minStemRadius, hash3(int(i), 7, 3), out);
    }
    if (cfg.leafKeep > 0) {
        for (const Leaf& l : set.leaves)
            if (l.keep < cfg.leafKeep) emitLeaf(l, leafTemplate(l.shape, cfg.simpleLeaves), cfg.leafScale, out);
    }
    if (cfg.flowerKeep > 0) {
        for (const Flower& f : set.flowers) {
            if (lod >= 2 && f.group < 0) continue;  // grass flowers vanish with their grass
            if (f.keep < cfg.flowerKeep) emitFlower(f, cfg.flowerScale, cfg.simpleFlowers, out);
        }
    }
    if (cfg.bladeKeep > 0) {
        for (const Blade& b : set.blades)
            if (b.keep < cfg.bladeKeep) emitBlade(b, cfg.bladeWidth, out);
    }
    if (cfg.puffLevel >= 0 && !set.clumps.empty()) {
        ClumpField field(set.clumps);
        if (cfg.mergeCell > 0) {
            std::vector<Clump> merged = mergeClumps(set.clumps, cfg.mergeCell, cfg.mergeClimbersOnly);
            for (const Clump& c : merged) emitPuff(c, field, cfg.puffLevel, cfg.puffShrink, out);
        } else {
            for (const Clump& c : set.clumps) emitPuff(c, field, cfg.puffLevel, cfg.puffShrink, out);
        }
    }
}

}  // namespace

void buildLod(const PlantSet& set, const Params& p, int lod, MeshBuffers& out) {
    buildWithConfig(set, configFor(lod, p), lod, out);
}

void buildFarLod(const PlantSet& set, const Params& p, float mergeCell, float minStemRadius, MeshBuffers& out) {
    LodConfig cfg = configFor(kLodCount - 1, p);
    cfg.mergeCell = mergeCell;
    cfg.minStemRadius = minStemRadius;
    buildWithConfig(set, cfg, kLodCount - 1, out);
}

}  // namespace plants
