// Bushes and grass tufts.
//
// A bush is a mound of overlapping spherical clumps (the cauliflower silhouette of anime foliage).
// Leaves cover only the outer surface of the clump union, branches run from the root to each clump,
// and the clumps themselves are kept for cloud shading and distant LODs.
#include "plants/generator.h"

namespace plants {

namespace {

const Vec3 kUp{0, 1, 0};

struct BushShape {
    float radius, height, clumpScale;
};

BushShape shapeFor(int style, float r) {
    switch (style) {
        case kBushTall: return {r * 0.72f, 1.7f, 0.9f};
        case kBushSpreading: return {r * 1.3f, 0.55f, 1.3f};
        default: return {r, 1.0f, 1.0f};
    }
}

}  // namespace

int growBush(const Params& p, Rng& rng, const Vec3& base, const Vec3& upIn, float scale, int style, PlantSet& out) {
    const int group = ++out.bushCount;  // group 0 is reserved for climbers
    const Vec3 up = normalize(upIn);
    const Frame fr = makeFrame(up, anyPerpendicular(up));
    auto toWorld = [&](const Vec3& l) { return base + fr.side * l.x + fr.normal * l.y + fr.forward * l.z; };

    BushShape shape = shapeFor(style, p[kParamBushRadius] * scale);
    const float R = shape.radius;
    const float H = R * p[kParamBushHeight] * 2.0f * shape.height;
    int clumpCount = int(p[kParamBushClumps] * (0.6f + 0.4f * scale) * shape.clumpScale + 0.5f);
    clumpCount = std::max(3, std::min(24, clumpCount));
    const float bushHue = rng.range(-0.5f, 0.5f);
    const float bushValue = rng.range(0.92f, 1.08f);
    const float bushPhase = rng.uniform();
    const float flexScale = 0.6f + 0.4f * std::min(1.5f, H);

    // Clumps: one crown plus a ring of lower clumps arranged with the golden angle.
    const size_t firstClump = out.clumps.size();
    auto addClump = [&](const Vec3& local, float r) {
        Clump c;
        c.center = toWorld(local);
        c.radii = {r, r * 0.92f, r};
        c.axis = up;
        c.group = group;
        c.mat = kMatBushLeaf;
        c.hue = bushHue + rng.range(-0.15f, 0.15f);
        c.value = bushValue;
        c.flex = 0.025f * flexScale;
        c.phase = bushPhase + rng.range(0.0f, 0.15f);
        out.clumps.push_back(c);
    };
    addClump({0, H * (style == kBushTall ? 0.74f : 0.66f), 0}, R * 0.52f);
    float angle = rng.range(0.0f, 2 * kPi);
    for (int i = 1; i < clumpCount; ++i) {
        angle += 2.39996f + rng.range(-0.3f, 0.3f);
        float elev = rng.range(-0.05f, 0.85f);
        float r = R * rng.range(0.36f, 0.52f);
        Vec3 local{std::cos(angle) * std::cos(elev) * R * 0.62f, H * 0.42f + std::sin(elev) * H * 0.42f,
                   std::sin(angle) * std::cos(elev) * R * 0.62f};
        local.y = std::max(local.y, r * 0.62f);
        addClump(local, r);
    }

    // Branches from the root to every clump, plus a few twigs inside each clump.
    auto addBranch = [&](const Vec3& from, const Vec3& to, float r0, float r1, float b0, float b1, int segments,
                         float flex0, float flex1) {
        Stem stem;
        Vec3 bend = rng.unitVector() * (length(to - from) * 0.15f);
        for (int s = 0; s <= segments; ++s) {
            float t = float(s) / segments;
            StemNode node;
            node.pos = lerp(from, to, t) + bend * (std::sin(t * kPi));
            node.normal = up;
            node.radius = lerpf(r0, r1, t);
            node.birth = lerpf(b0, b1, t);
            node.flex = lerpf(flex0, flex1, t);
            node.parent = s == 0 ? -1 : int(out.nodes.size() - 1);
            out.nodes.push_back(node);
            stem.nodes.push_back(int(out.nodes.size() - 1));
        }
        out.stems.push_back(stem);
    };
    for (size_t ci = firstClump; ci < out.clumps.size(); ++ci) {
        const Clump& c = out.clumps[ci];
        Vec3 root = toWorld({rng.range(-0.08f, 0.08f) * R, 0.0f, rng.range(-0.08f, 0.08f) * R});
        addBranch(root, c.center, std::max(0.008f, R * 0.035f), 0.006f, 0.0f, 0.3f, 4, 0.0f, c.flex * 0.6f);
        for (int k = 0; k < 3; ++k) {
            Vec3 d = normalize(rng.unitVector() + up * 0.6f);
            addBranch(c.center, c.center + d * (c.radii.x * 0.78f), 0.005f, 0.0025f, 0.3f, 0.45f, 2, c.flex * 0.6f,
                      c.flex);
        }
    }

    // Leaves on the outer surface of the clump union.
    const float density = p[kParamBushLeafDensity];
    const float leafSize = p[kParamBushLeafSize];
    for (size_t ci = firstClump; ci < out.clumps.size(); ++ci) {
        const Clump c = out.clumps[ci];
        float r = c.radii.x;
        int count = int(4 * kPi * r * r * density * 0.8f);
        for (int k = 0; k < count; ++k) {
            Vec3 d = rng.unitVector();
            Vec3 pos = c.center + Vec3{d.x * r, d.y * c.radii.y, d.z * r} * rng.range(0.8f, 1.03f);
            float height = dot(pos - base, up);
            if (height < 0.03f) continue;
            bool buried = false;
            for (size_t cj = firstClump; cj < out.clumps.size() && !buried; ++cj) {
                if (cj == ci) continue;
                const Clump& o = out.clumps[cj];
                buried = length2(pos - o.center) < (o.radii.x * 0.9f) * (o.radii.x * 0.9f);
            }
            if (buried) continue;
            float h01 = saturate(height / std::max(H, 0.05f));
            Leaf leaf;
            leaf.pos = pos;
            leaf.normal = normalize(d + rng.unitVector() * 0.55f + up * 0.15f, d);
            leaf.forward = normalize(projectOnPlane(rng.unitVector() + up * 0.3f, leaf.normal), anyPerpendicular(leaf.normal));
            leaf.size = leafSize * rng.range(0.75f, 1.2f);
            leaf.birth = saturate(0.35f + 0.55f * h01 + rng.range(0.0f, 0.1f));
            leaf.flex = c.flex * (0.5f + 0.7f * h01);
            leaf.phase = c.phase + rng.range(0.0f, 0.2f);
            leaf.hue = c.hue + rng.range(-0.2f, 0.2f) + std::max(0.0f, d.y) * 0.25f;
            leaf.value = c.value * rng.range(0.9f, 1.1f);
            leaf.ao = lerpf(0.55f, 1.0f, h01);
            leaf.keep = rng.uniform();
            leaf.group = group;
            leaf.mat = kMatBushLeaf;
            leaf.shape = kShapeBush;
            out.leaves.push_back(leaf);
        }
    }

    if (style == kBushFlowering) {
        uint8_t mat = rng.chance(0.6f) ? kMatFlowerWhite : kMatFlowerYellow;
        int count = int(10 + 30 * R * R);
        for (int k = 0; k < count; ++k) {
            const Clump& c = out.clumps[firstClump + rng.rangeInt(0, int(out.clumps.size() - firstClump) - 1)];
            Vec3 d = normalize(rng.unitVector() + up * 0.5f);
            Flower f;
            f.pos = c.center + Vec3{d.x * c.radii.x, d.y * c.radii.y, d.z * c.radii.z} * 1.02f;
            f.normal = normalize(d + up * 0.4f);
            f.size = rng.range(0.045f, 0.07f);
            f.birth = saturate(0.85f + rng.range(0.0f, 0.15f));
            f.flex = c.flex;
            f.phase = c.phase;
            f.keep = rng.uniform();
            f.group = group;
            f.mat = rng.chance(0.85f) ? mat : (mat == kMatFlowerWhite ? kMatFlowerYellow : kMatFlowerWhite);
            out.flowers.push_back(f);
        }
    }
    return group;
}

void growGrassTuft(const Params& p, Rng& rng, PlantSet& out) {
    int count = int(p[kParamGrassBlades] * rng.range(0.8f, 1.2f) + 0.5f);
    float height = p[kParamGrassHeight];
    float tuftHue = rng.range(-0.4f, 0.4f);
    for (int i = 0; i < count; ++i) {
        float a = rng.range(0.0f, 2 * kPi);
        float rad = 0.11f * std::sqrt(rng.uniform());
        Vec3 outward{std::cos(a), 0, std::sin(a)};
        float tilt = rng.range(0.08f, 0.45f);
        Blade b;
        b.base = outward * rad;
        b.dir = normalize(outward * tilt + kUp);
        float twist = a + rng.range(-0.6f, 0.6f);
        b.side = normalize(cross(kUp, Vec3{std::cos(twist), 0, std::sin(twist)}));
        b.height = height * rng.range(0.55f, 1.15f);
        b.width = rng.range(0.03f, 0.046f);
        b.bend = rng.range(0.15f, 0.55f);
        b.hue = tuftHue + rng.range(-0.25f, 0.25f);
        b.value = rng.range(0.9f, 1.1f);
        b.phase = rng.uniform();
        b.keep = rng.uniform();
        out.blades.push_back(b);
    }
    // Wild flowers on thin stalks, like the white and yellow dots in the concept art.
    if (rng.chance(p[kParamGrassFlowerChance])) {
        int flowers = rng.rangeInt(1, 3);
        uint8_t mat = rng.chance(0.6f) ? kMatFlowerWhite : kMatFlowerYellow;
        for (int i = 0; i < flowers; ++i) {
            float a = rng.range(0.0f, 2 * kPi);
            Vec3 outward{std::cos(a), 0, std::sin(a)};
            Blade stalk;
            stalk.base = outward * rng.range(0.0f, 0.06f);
            stalk.dir = normalize(outward * rng.range(0.05f, 0.25f) + kUp);
            stalk.side = normalize(cross(kUp, outward));
            stalk.height = height * rng.range(1.0f, 1.5f);
            stalk.width = 0.009f;
            stalk.bend = 0.1f;
            stalk.hue = tuftHue;
            stalk.value = 0.95f;
            stalk.phase = rng.uniform();
            stalk.keep = 0.0f;  // stalks stay as long as their flower does
            out.blades.push_back(stalk);
            Flower f;
            // Same curve as the mesher's blade tip: base + dir*h + bendDir*bend*h, bendDir = side x up.
            f.pos = stalk.base + stalk.dir * stalk.height + normalize(cross(stalk.side, kUp)) * (stalk.bend * stalk.height);
            f.normal = normalize(kUp + rng.unitVector() * 0.35f);
            f.size = rng.range(0.045f, 0.065f);
            f.birth = 0.0f;
            f.flex = 0.06f;
            f.phase = stalk.phase;
            f.keep = 0.0f;
            f.group = -1;
            f.mat = mat;
            out.flowers.push_back(f);
        }
    }
}

}  // namespace plants
