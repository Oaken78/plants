// Trees: round broadleaf trees and oaks.
//
// The crown comes first: overlapping clumps in a wide rounded envelope (the same cloud-shaded foliage as the
// bushes, so trees read as soft anime masses). The skeleton is then built to carry it: a slightly curved trunk to a
// fork, a few limbs that each serve the clumps in one direction, a branch into every clump and twigs inside it.
// Branch thickness follows the pipe model (radius ~ sqrt of the foliage a branch carries).
// Oaks are bigger and gnarlier, and their form follows how crowded they are: alone they grow a short trunk, a low
// fork and long near-horizontal limbs under a crown wider than the tree is tall; near trees or walls they grow tall
// with a high clear trunk and a narrow crown.
//
// Roots follow the host's surfaces. On a roof or ledge less than tree.rootReach above the ground they steer toward
// the ground: across the roof, up and over parapets, down the walls, and into the soil. On the way they fork into
// child roots that also seek the ground (a fig-like net over the walls) and sprout thin side rootlets; surface roots
// fork as they spread. Higher up they only spread over the surface. Trees without a host get a forked root flare.
#include <algorithm>
#include "plants/generator.h"

namespace plants {

namespace {

const Vec3 kUp{0, 1, 0};
const Vec3 kDown{0, -1, 0};

// Nodes go straight into the PlantSet; foliage weight and path length are tracked alongside them.
struct Skeleton {
    PlantSet& out;
    size_t first;
    std::vector<float> weight;  // foliage carried by each node (branch ends get their clump's area)
    std::vector<float> path;    // distance from the tree's root node along the branches

    explicit Skeleton(PlantSet& o) : out(o), first(o.nodes.size()) {}

    int add(const Vec3& pos, const Vec3& normal, int parent) {
        StemNode n;
        n.pos = pos;
        n.normal = normal;
        n.parent = parent;
        float d = parent >= int(first) ? path[parent - first] + length(pos - out.nodes[parent].pos) : 0.0f;
        out.nodes.push_back(n);
        weight.push_back(0);
        path.push_back(d);
        return int(out.nodes.size() - 1);
    }
    float& w(int node) { return weight[node - first]; }

    std::vector<int> lastBranch;  // nodes of the most recent branch() call, start node first

    // A gently curved bark branch from an existing node to `to`; returns its end node.
    // jitter displaces the inner nodes by that fraction of the length (gnarled oaks use more).
    int branch(int from, const Vec3& to, int segments, const Vec3& bend, float jitter, Rng& rng) {
        Stem stem;
        stem.mat = kMatBark;
        stem.nodes.push_back(from);
        const Vec3 a = out.nodes[from].pos;
        const float len = length(to - a);
        int prev = from;
        for (int s = 1; s <= segments; ++s) {
            float t = float(s) / segments;
            Vec3 pos = lerp(a, to, t) + bend * std::sin(t * kPi);
            if (s < segments) pos += rng.inUnitSphere() * (len * jitter);
            prev = add(pos, kUp, prev);
            stem.nodes.push_back(prev);
        }
        lastBranch = stem.nodes;
        out.stems.push_back(stem);
        return prev;
    }
};

// Horizontal direction toward open space around base (zero when open on all sides): trees lean away from walls.
Vec3 openSky(const TriangleBvh* host, const Vec3& base, float reach) {
    if (!host) return {};
    Vec3 sum;
    for (int k = 0; k < 8; ++k) {
        float a = k * (2 * kPi / 8);
        Vec3 d{std::cos(a), 0, std::sin(a)};
        SurfaceHit h;
        float free = host->raycast(base + kUp * 1.0f, d, reach, h, kMaskSolid | kMaskNoGrow) ? h.distance : reach;
        sum += d * (free / reach);
    }
    return sum * 0.25f;
}

}  // namespace

float soilArea(const TriangleBvh& bvh, const Vec3& base) {
    const float cell = 0.5f, radius = 7.0f;
    const Vec3 from = base + kUp * 0.25f;
    int count = 0;
    for (float dz = -radius; dz <= radius; dz += cell) {
        for (float dx = -radius; dx <= radius; dx += cell) {
            if (dx * dx + dz * dz > radius * radius) continue;
            const Vec3 q{base.x + dx, base.y, base.z + dz};
            SurfaceHit h;
            // Soil spreads, and roots follow it, only where no wall or parapet is in the way.
            Vec3 d = q + kUp * 0.25f - from;
            float len = length(d);
            if (len > 1e-3f && bvh.raycast(from, d / len, len, h, kMaskSolid | kMaskNoGrow)) continue;
            // A flat surface at about the same height under that spot.
            if (!bvh.raycast(q + kUp * 0.5f, kDown, 1.0f, h, kMaskSolid | kMaskGround) || h.normal.y < 0.8f) continue;
            if (h.tag == kTagGround) return 1e9f;
            ++count;
        }
    }
    return count * cell * cell;
}

namespace {

struct RootContext {
    const TriangleBvh& bvh;
    Rng& rng;
    Skeleton& sk;
    float branching;  // tree.rootBranching
    int budget;       // root nodes left for this tree
};

// One root strand hugging the host. With seek it steers toward target, climbs over anything facing away from the
// target (parapets), and ends shortly after reaching ground. Returns true if it reached the ground.
bool crawlRoot(RootContext& c, int parent, Vec3 pos, Vec3 dir, const Vec3& target, bool seek, float maxLen, float r0,
               float rEnd, int depth) {
    const float step = 0.12f;
    Stem stem;
    stem.mat = kMatBark;
    stem.nodes.push_back(parent);
    float travelled = 0, lastFork = 0;
    bool reached = false;
    uint8_t lastTag = 255;  // tag of the surface under the previous node
    int prev = parent;
    while (travelled < maxLen && c.budget > 0) {
        const float t = travelled / maxLen;
        const float radius = lerpf(r0, rEnd, t);
        // Roots sit on walls but are half sunk into the soil.
        const float off = lastTag == kTagGround ? radius * 0.3f : radius * 0.75f + 0.01f;
        SurfaceHit h;
        bool near = c.bvh.closestPoint(pos, 0.8f, h, kMaskSolid | kMaskGround);
        Vec3 n = near ? (h.distance > 1e-4f ? normalize(pos - h.point, h.normal) : h.normal) : kUp;
        Vec3 want = dir;
        if (seek && !reached) {
            want = target - pos;
            Vec3 across = projectOnPlane(want, kUp), nh = projectOnPlane(n, kUp);
            // On a wall that faces away from the target (inside of a parapet), climb over it first.
            if (near && std::fabs(n.y) < 0.5f && dot(normalize(nh), normalize(across)) < -0.3f) want = kUp;
        }
        Vec3 steer = normalize(near ? projectOnPlane(want, n) : want, dir);
        Vec3 d = dir + steer * 1.4f + c.rng.unitVector() * 0.3f;
        if (near) d -= n * (saturate((h.distance - off) / 0.8f) * 2.0f);
        else d += kDown;
        d = normalize(d, dir);

        Vec3 np = pos + d * step;
        SurfaceHit hit;
        if (c.bvh.raycast(pos, d, step + off, hit, kMaskAll)) {
            Vec3 slide = normalize(projectOnPlane(d, hit.normal), anyPerpendicular(hit.normal));
            np = hit.point + hit.normal * off + slide * (step * 0.5f);
            d = slide;
        }
        SurfaceHit sh;
        lastTag = 255;
        if (c.bvh.closestPoint(np, radius * 3 + 0.1f, sh, kMaskSolid | kMaskGround)) {
            n = sh.distance > 1e-4f ? normalize(np - sh.point, sh.normal) : sh.normal;
            np = sh.point + n * off;
            lastTag = sh.tag;
        }
        pos = np;
        dir = d;
        travelled += step;
        prev = c.sk.add(pos, n, prev);
        StemNode& node = c.sk.out.nodes[prev];
        node.radius = radius;
        node.birth = saturate(0.05f + 0.5f * t);
        stem.nodes.push_back(prev);
        --c.budget;

        if (seek && !reached && lastTag == kTagGround) {
            reached = true;
            maxLen = travelled + c.rng.range(0.3f, 0.9f);  // a little way along the soil, then down into it
        }
        // Forks: a root heading for the ground splits into another one aimed a little to the side, so the roots
        // fan out into a net as they run down a wall; roots spreading over a surface fork into two.
        const float sign = c.rng.chance(0.5f) ? 1.0f : -1.0f;
        if (depth < 3 && travelled - lastFork > 0.5f && c.rng.chance(0.07f * c.branching / (1.0f + depth * 1.5f))) {
            Vec3 childDir = normalize(rotateAxis(d, n, sign * c.rng.range(0.5f, 0.9f)), d);
            float childR = radius * c.rng.range(0.55f, 0.75f);
            if (seek && !reached) {
                Vec3 side = normalize(projectOnPlane(cross(n, d), kUp), anyPerpendicular(kUp));
                Vec3 childTarget = target + side * (sign * c.rng.range(1.0f, 2.5f));
                crawlRoot(c, prev, pos, childDir, childTarget, true, (maxLen - travelled) * 1.2f + 1.0f, childR,
                          childR * 0.55f, depth + 1);
            } else {
                crawlRoot(c, prev, pos, childDir, target, false, std::max(0.4f, (maxLen - travelled) * c.rng.range(0.5f, 0.9f)),
                          childR, childR * 0.25f, depth + 1);
            }
            lastFork = travelled;
        }
        // Side rootlets: short thin roots that web the surface around the bigger ones.
        if (depth < 3 && travelled > 0.4f && c.rng.chance(0.06f * c.branching)) {
            Vec3 side = normalize(rotateAxis(d, n, -sign * c.rng.range(0.9f, 1.5f)), d);
            crawlRoot(c, prev, pos, side, target, false, c.rng.range(0.3f, 1.1f), radius * 0.4f, 0.006f, 3);
        }
    }
    if (stem.nodes.size() > 1 && lastTag == kTagGround) c.sk.out.nodes[prev].pos -= kUp * 0.2f;
    c.sk.out.stems.push_back(stem);
    return reached;
}

void growRoots(const Params& p, Rng& rng, const TriangleBvh* host, const Vec3& base, float H, float trunkR, int rootNode,
               Skeleton& sk) {
    int n = int(p.i(kParamTreeRoots) * (0.6f + 0.4f * std::min(H / 7.0f, 1.5f)) + 0.5f);
    n = std::max(0, std::min(16, n));
    if (n == 0) return;
    const float a0 = rng.range(0.0f, 2 * kPi);

    if (!host) {
        // Free-standing tree: a short flare of roots that dip into the soil.
        for (int i = 0; i < n; ++i) {
            float a = a0 + i * (2 * kPi / n) + rng.range(-0.3f, 0.3f);
            Vec3 dir{std::cos(a), 0, std::sin(a)};
            float len = H * rng.range(0.08f, 0.16f) + trunkR * 2.0f;
            float r0 = trunkR * rng.range(0.35f, 0.55f);
            Stem stem;
            stem.mat = kMatBark;
            stem.nodes.push_back(rootNode);
            int prev = rootNode;
            std::vector<int> nodes;
            for (int s = 1; s <= 5; ++s) {
                float t = s / 5.0f;
                Vec3 pos = base + dir * (trunkR * 0.5f + len * t) + kUp * (0.1f * (1 - t) - 0.3f * t * t) +
                           projectOnPlane(rng.unitVector(), kUp) * (len * 0.05f);
                prev = sk.add(pos, kUp, prev);
                sk.out.nodes[prev].radius = r0 * (1.0f - 0.8f * t);
                sk.out.nodes[prev].birth = 0.05f + 0.2f * t;
                stem.nodes.push_back(prev);
                nodes.push_back(prev);
            }
            sk.out.stems.push_back(stem);
            // Forks off the flare: one or two side roots that split away and dive sooner.
            int forks = int(p[kParamTreeRootBranching] * rng.range(0.5f, 2.5f));
            for (int f = 0; f < forks; ++f) {
                int from = nodes[rng.rangeInt(1, 2)];
                Vec3 start = sk.out.nodes[from].pos;
                float fr0 = sk.out.nodes[from].radius * 0.7f;
                Vec3 fdir = rotateAxis(dir, kUp, (rng.chance(0.5f) ? 1.0f : -1.0f) * rng.range(0.5f, 1.0f));
                Stem fork;
                fork.mat = kMatBark;
                fork.nodes.push_back(from);
                int fprev = from;
                for (int s = 1; s <= 3; ++s) {
                    float t = s / 3.0f;
                    Vec3 pos = start + fdir * (len * 0.5f * t) + kUp * (-0.25f * t * t);
                    fprev = sk.add(pos, kUp, fprev);
                    sk.out.nodes[fprev].radius = fr0 * (1.0f - 0.75f * t);
                    sk.out.nodes[fprev].birth = 0.15f + 0.15f * t;
                    fork.nodes.push_back(fprev);
                }
                sk.out.stems.push_back(fork);
            }
        }
        return;
    }

    // Is the ground within reach? Measure from the nearest ground surface (or the host's lowest point).
    SurfaceHit g;
    float groundY = host->closestPoint(base, 80.0f, g, kMaskGround) ? g.point.y : host->bounds().lo.y;
    float above = base.y - groundY;
    bool seek = above > 0.6f && above < p[kParamTreeRootReach];
    RootContext ctx{*host, rng, sk, p[kParamTreeRootBranching], 3000};
    for (int i = 0; i < n; ++i) {
        float a = a0 + i * (2 * kPi / n) + rng.range(-0.25f, 0.25f);
        Vec3 dir{std::cos(a), 0, std::sin(a)};
        bool seekThis = seek && (i % 3 != 2);  // a third of the roots stay up and spread
        Vec3 target = base;
        if (seekThis) {
            Vec3 probe{base.x + dir.x * 3.0f, groundY, base.z + dir.z * 3.0f};
            SurfaceHit tg;
            target = host->closestPoint(probe, 30.0f, tg, kMaskGround) ? tg.point : probe;
        }
        float r0 = trunkR * rng.range(0.45f, 0.7f);
        bool onGround = above <= 0.6f;
        float maxLen = seekThis ? above * 2.0f + 5.0f : H * rng.range(onGround ? 0.07f : 0.15f, onGround ? 0.14f : 0.3f) + 0.4f;
        float rEnd = seekThis ? r0 * 0.6f : r0 * 0.25f;
        crawlRoot(ctx, rootNode, base + dir * (trunkR * 0.6f) + kUp * 0.04f, normalize(dir + kDown * 0.3f), target,
                  seekThis, maxLen, r0, rEnd, 0);
    }
}

}  // namespace

namespace {

// Everything that differs between species. Crowding (0 = standing alone, 1 = hemmed in by trees or walls) makes
// trees taller and narrower, the way real trees compete for light; oaks respond strongly, round trees a little.
struct TreeShape {
    float H, R, Rv, forkH, trunkR, flare, jitter, wobble;
    int clumps;
    float clumpMin, clumpMax, elevMin, elevMax;
    int limbs;           // 0 = derive from the clump count
    float limbReach;     // how far limbs reach toward their clumps before branching (0..1)
    int trunkSegs, limbSegs;
    uint8_t leafMat, leafShape;
    float leafSize, leafDensity;
};

TreeShape shapeFor(const Params& p, Rng& rng, int species, float size, float crowding) {
    const float c = saturate(crowding);
    TreeShape s;
    if (species == kTreeOak) {
        s.H = p[kParamOakHeight] * size * rng.range(0.85f, 1.15f) * lerpf(0.9f, 1.3f, c);
        s.R = s.H * lerpf(0.78f, 0.36f, c) * rng.range(0.9f, 1.1f);  // alone: crown wider than the tree is tall
        s.Rv = s.R * lerpf(0.55f, 0.95f, c);
        s.forkH = s.H * lerpf(rng.range(0.16f, 0.24f), rng.range(0.38f, 0.48f), c);
        s.trunkR = s.H * p[kParamOakTrunk] * rng.range(0.9f, 1.1f) * lerpf(1.15f, 0.85f, c);
        s.flare = 1.0f;
        s.jitter = 0.07f;
        s.wobble = 0.035f;
        s.clumps = std::max(8, std::min(36, int(p[kParamTreeClumps] * 1.7f * (0.7f + 0.3f * size) + 0.5f)));
        s.clumpMin = 0.26f;
        s.clumpMax = 0.38f;
        s.elevMin = -0.4f;
        s.elevMax = 0.55f;
        s.limbs = int(lerpf(6.0f, 3.0f, c) + 0.5f);
        s.limbReach = lerpf(0.8f, 0.55f, c);
        s.trunkSegs = 7;
        s.limbSegs = 6;
        s.leafMat = kMatOakLeaf;
        s.leafShape = kShapeOak;
        s.leafSize = p[kParamOakLeafSize] * (0.8f + 0.2f * std::min(s.H / 7.0f, 3.0f));
        s.leafDensity = p[kParamOakLeafDensity];
    } else {
        s.H = p[kParamTreeHeight] * size * rng.range(0.85f, 1.15f) * lerpf(0.95f, 1.12f, c);
        s.R = s.H * p[kParamTreeCanopy] * rng.range(0.85f, 1.15f) * lerpf(1.08f, 0.82f, c);
        s.Rv = s.R * 0.75f;
        s.forkH = s.H * rng.range(0.26f, 0.38f);
        s.trunkR = s.H * p[kParamTreeTrunk] * rng.range(0.85f, 1.15f);
        s.flare = 0.6f;
        s.jitter = 0.03f;
        s.wobble = 0.025f;
        s.clumps = std::max(4, std::min(26, int(p[kParamTreeClumps] * (0.55f + 0.45f * size) + 0.5f)));
        s.clumpMin = 0.34f;
        s.clumpMax = 0.5f;
        s.elevMin = -0.55f;
        s.elevMax = 0.65f;
        s.limbs = 0;
        s.limbReach = 0.6f;
        s.trunkSegs = 6;
        s.limbSegs = 4;
        s.leafMat = kMatTreeLeaf;
        s.leafShape = kShapeBush;
        s.leafSize = p[kParamTreeLeafSize] * (0.8f + 0.3f * std::min(size, 2.0f));
        s.leafDensity = p[kParamTreeLeafDensity];
    }
    s.trunkR = std::max(0.03f, s.trunkR);
    return s;
}

}  // namespace

int growTree(const Params& p, Rng& rng, const TriangleBvh* host, const Vec3& base, float size, int species, float crowding,
             PlantSet& out, float maxHeight) {
    const int group = out.nextGroup++;
    ++out.treeCount;
    TreeShape s = shapeFor(p, rng, species, size, crowding);
    // Cap the height (soil) and fit under overhangs, scaling the whole tree so it keeps its proportions.
    float limit = maxHeight;
    if (host) {
        SurfaceHit h;
        if (host->raycast(base + kUp * 0.3f, kUp, s.H * 1.15f, h, kMaskSolid | kMaskNoGrow))
            limit = std::min(limit, std::max(1.2f, (h.distance + 0.3f) * 0.85f));
    }
    if (s.H > limit) {
        float k = limit / s.H;
        s.H *= k;
        s.R *= k;
        s.Rv *= k;
        s.forkH *= k;
        s.trunkR = std::max(0.03f, s.trunkR * k);
    }
    const float H = s.H, R = s.R, Rv = s.Rv, forkH = s.forkH, trunkR = s.trunkR;
    const float sizeFactor = H / 7.0f;
    Vec3 lean = openSky(host, base, std::max(3.0f, H * 0.6f)) * p[kParamTreeLean];
    if (length(lean) > 0.45f) lean = normalize(lean) * 0.45f;
    const Vec3 axis = normalize(kUp + lean + projectOnPlane(rng.unitVector(), kUp) * 0.08f);
    const Frame fr = makeFrame(axis, anyPerpendicular(axis));
    const Vec3 crown = base + axis * (H - Rv * 1.0f);
    const float hue = rng.range(-0.45f, 0.45f), value = rng.range(0.92f, 1.08f), phase = rng.uniform();
    const float flexScale = std::min(2.0f, 0.6f + 0.4f * sizeFactor);

    // Crown clumps: a top clump and a ring placed with the golden angle, all above the fork.
    const size_t firstClump = out.clumps.size();
    auto addClump = [&](Vec3 pos, float r) {
        if (host) {
            // Keep the crown out of walls it grows next to.
            SurfaceHit h;
            if (host->closestPoint(pos, r * 0.8f, h, kMaskSolid | kMaskNoGrow))
                pos += normalize(pos - h.point, h.normal) * (r * 0.8f - h.distance);
        }
        Clump c;
        c.center = pos;
        c.radii = {r, r * 0.9f, r};
        c.axis = axis;
        c.group = group;
        c.mat = s.leafMat;
        c.hue = hue + rng.range(-0.15f, 0.15f);
        c.value = value;
        c.flex = (0.03f + 0.05f * saturate(dot(pos - base, axis) / H)) * flexScale;
        c.phase = phase + rng.range(0.0f, 0.2f);
        out.clumps.push_back(c);
    };
    addClump(crown + axis * (Rv * 0.42f), R * (species == kTreeOak ? 0.42f : 0.52f));
    float ang = rng.range(0.0f, 2 * kPi);
    for (int i = 1; i < s.clumps; ++i) {
        ang += 2.39996f + rng.range(-0.35f, 0.35f);
        float elev = rng.range(s.elevMin, s.elevMax);
        float r = R * rng.range(s.clumpMin, s.clumpMax);
        float reach = rng.range(0.55f, 0.75f);
        Vec3 pos = crown + fr.side * (std::cos(ang) * std::cos(elev) * R * reach) + fr.normal * (std::sin(elev) * Rv * 0.72f) +
                   fr.forward * (std::sin(ang) * std::cos(elev) * R * reach);
        float minH = forkH + r * 0.7f;
        float hh = dot(pos - base, axis);
        if (hh < minH) pos += axis * (minH - hh);
        addClump(pos, r);
    }

    // Skeleton: trunk with a slight S-curve up to the fork.
    Skeleton sk(out);
    const int root = sk.add(base - axis * 0.05f, axis, -1);
    Stem trunk;
    trunk.mat = kMatBark;
    trunk.nodes.push_back(root);
    const float phi = rng.range(0.0f, 2 * kPi);
    int prev = root;
    for (int seg = 1; seg <= s.trunkSegs; ++seg) {
        float t = float(seg) / s.trunkSegs;
        Vec3 wobble = (fr.side * std::cos(phi + t * 2.2f) + fr.forward * std::sin(phi + t * 2.2f)) * (H * s.wobble * std::sin(t * kPi));
        prev = sk.add(base + axis * (forkH * t) + wobble, axis, prev);
        trunk.nodes.push_back(prev);
    }
    out.stems.push_back(trunk);
    const int fork = prev;
    const Vec3 forkPos = out.nodes[fork].pos;

    auto twigs = [&](int from, const Clump& c) {
        for (int k = 0; k < 3; ++k) {
            Vec3 d = normalize(rng.unitVector() + kUp * 0.5f);
            int e = sk.branch(from, c.center + d * (c.radii.x * 0.8f), 2, Vec3{}, s.jitter, rng);
            sk.w(e) += c.radii.x * c.radii.x * 0.05f;
        }
    };
    // Limbs: ring clumps sorted by direction around the trunk, split into a few contiguous groups. Each clump is
    // fed by a branch from the point of its limb nearest to it, so long limbs branch along their length.
    std::vector<int> ring;
    for (size_t ci = firstClump + 1; ci < out.clumps.size(); ++ci) ring.push_back(int(ci));
    auto azimuth = [&](int ci) {
        Vec3 d = out.clumps[ci].center - forkPos;
        return std::atan2(dot(d, fr.forward), dot(d, fr.side));
    };
    std::sort(ring.begin(), ring.end(), [&](int a, int b) { return azimuth(a) < azimuth(b); });
    std::rotate(ring.begin(), ring.begin() + rng.rangeInt(0, int(ring.size()) - 1), ring.end());
    int limbs = s.limbs > 0 ? s.limbs : int(ring.size() / 3.5f + 0.5f);
    limbs = std::max(1, std::min(std::min(7, int(ring.size())), limbs));
    for (int l = 0; l < limbs; ++l) {
        size_t b0 = ring.size() * l / limbs, b1 = ring.size() * (l + 1) / limbs;
        Vec3 centroid;
        for (size_t i = b0; i < b1; ++i) centroid += out.clumps[ring[i]].center;
        centroid = centroid / float(b1 - b0);
        Vec3 limbEnd = lerp(forkPos, centroid, s.limbReach) + axis * (R * 0.06f);
        sk.branch(fork, limbEnd, s.limbSegs, axis * (R * 0.1f), s.jitter, rng);
        const std::vector<int> limbNodes = sk.lastBranch;
        for (size_t i = b0; i < b1; ++i) {
            const Clump c = out.clumps[ring[i]];
            int from = limbNodes.back();
            float best = 1e30f;
            for (size_t k = limbNodes.size() / 2; k < limbNodes.size(); ++k) {
                float d2 = length2(out.nodes[limbNodes[k]].pos - c.center);
                if (d2 < best) { best = d2; from = limbNodes[k]; }
            }
            int e = sk.branch(from, lerp(out.nodes[from].pos, c.center, 0.88f), 3, axis * (c.radii.x * 0.15f), s.jitter, rng);
            sk.w(e) += c.radii.x * c.radii.x;
            twigs(e, c);
        }
    }
    {
        const Clump c = out.clumps[firstClump];
        int e = sk.branch(fork, lerp(forkPos, c.center, 0.9f), s.limbSegs, Vec3{}, s.jitter, rng);
        sk.w(e) += c.radii.x * c.radii.x;
        twigs(e, c);
    }

    // Pipe model radii, a flared base, growth order and wind flex along the branches.
    const size_t end = out.nodes.size();
    for (size_t i = end; i-- > sk.first + 1;) {
        int parent = out.nodes[i].parent;
        if (parent >= int(sk.first)) sk.weight[parent - sk.first] += sk.weight[i - sk.first];
    }
    const float total = std::max(1e-4f, sk.weight[0]);
    float maxPath = 1e-3f;
    for (float d : sk.path) maxPath = std::max(maxPath, d);
    const float flareH = std::min(H * 0.12f, forkH * 0.6f);
    for (size_t i = sk.first; i < end; ++i) {
        StemNode& node = out.nodes[i];
        float h = dot(node.pos - base, axis);
        node.radius = std::max(0.006f, trunkR * std::sqrt(sk.weight[i - sk.first] / total));
        if (h < flareH) node.radius *= 1.0f + s.flare * (1.0f - h / flareH) * (1.0f - h / flareH);
        float t = sk.path[i - sk.first] / maxPath;
        node.birth = 0.05f + 0.5f * t;
        node.flex = 0.07f * t * t * flexScale;
    }

    // Foliage on the crown. Very large crowns get fewer, bigger leaves so a tree stays within tree.maxLeaves.
    float crownBottom = H, area = 0;
    for (size_t ci = firstClump; ci < out.clumps.size(); ++ci) {
        const Clump& c = out.clumps[ci];
        crownBottom = std::min(crownBottom, dot(c.center - base, axis) - c.radii.x * 0.9f);
        area += 4 * kPi * c.radii.x * c.radii.x * 0.8f * 0.45f;  // roughly the exposed part of each clump
    }
    float leafSize = s.leafSize, density = s.leafDensity;
    const float expected = area * density;
    if (expected > p[kParamTreeMaxLeaves]) {
        float k = p[kParamTreeMaxLeaves] / expected;
        density *= k;
        leafSize /= std::sqrt(k);
    }
    const size_t firstLeaf = out.leaves.size();
    coverClumpsWithLeaves(rng, out, firstClump, base, axis, crownBottom, H, leafSize, density, s.leafMat, s.leafShape,
                          group, 0.55f);
    if (host) {
        // Drop leaves that ended up on the far side of a wall from the trunk.
        size_t kept = firstLeaf;
        for (size_t i = firstLeaf; i < out.leaves.size(); ++i) {
            const Leaf& leaf = out.leaves[i];
            Vec3 from = base + axis * std::max(0.3f, dot(leaf.pos - base, axis));
            Vec3 d = leaf.pos - from;
            float len = length(d);
            SurfaceHit h;
            if (len > 1e-3f && host->raycast(from, d / len, len, h, kMaskSolid | kMaskNoGrow)) continue;
            out.leaves[kept++] = leaf;
        }
        out.leaves.resize(kept);
    }

    growRoots(p, rng, host, base, H, trunkR, root, sk);
    return group;
}

float estimateCrowding(const TriangleBvh& bvh, const Vec3& base, const std::vector<Vec3>& trunks) {
    // Walls: rays around the tree at two heights, weighted by how close they hit.
    const float reach = 9.0f;
    float blocked = 0;
    for (int k = 0; k < 12; ++k) {
        float a = k * (2 * kPi / 12);
        Vec3 d{std::cos(a), 0, std::sin(a)};
        for (float h : {1.5f, 4.0f}) {
            SurfaceHit hit;
            if (bvh.raycast(base + kUp * h, d, reach, hit, kMaskSolid | kMaskNoGrow)) blocked += 1.0f - hit.distance / reach;
        }
    }
    float walls = saturate(blocked / 24.0f * 2.5f);
    // Other trees.
    float neighbours = 0;
    for (const Vec3& t : trunks) neighbours += std::max(0.0f, 1.0f - length(projectOnPlane(t - base, kUp)) / 10.0f);
    return saturate(std::max(walls, neighbours * 0.6f));
}

void scatterTreesOnHost(const TriangleBvh& bvh, const Params& p, Rng& rng, PlantSet& out, std::vector<Vec3>& trunks) {
    const float floorY = bvh.bounds().lo.y;
    const float oakShare = p[kParamTreeOakShare];
    auto spaced = [&](const Vec3& q, float m) {
        for (const Vec3& t : trunks)
            if (length2(projectOnPlane(t - q, kUp)) < m * m) return false;
        return true;
    };

    // Roofs and ledges with head room, preferably close to an edge so the roots can find their way down.
    SurfaceSampler solid(bvh, kMaskSolid);
    const int wanted = p.i(kParamTreeCount);
    int placed = 0;
    for (int attempt = 0; !solid.empty() && attempt < wanted * 300 && placed < wanted; ++attempt) {
        SurfaceSample s = solid.sample(rng);
        if (s.normal.y < 0.85f || s.point.y < floorY + 1.0f) continue;
        SurfaceHit h;
        float clear = bvh.raycast(s.point + kUp * 0.05f, kUp, 12.0f, h) ? h.distance : 12.0f;
        if (clear < 2.5f) continue;
        bool nearEdge = false;
        for (int k = 0; k < 8 && !nearEdge; ++k) {
            float a = k * (2 * kPi / 8);
            SurfaceHit g;
            nearEdge = !bvh.raycast(s.point + Vec3{std::cos(a) * 1.8f, 0.4f, std::sin(a) * 1.8f}, kDown, 1.0f, g, kMaskSolid);
        }
        if (!nearEdge && !rng.chance(0.2f)) continue;
        if (!spaced(s.point, 3.5f) || isEnclosed(bvh, s.point)) continue;
        // How big a tree this spot can feed: nothing on a window sill, a small tree on a balcony, more on a roof.
        float soil = soilArea(bvh, s.point);
        if (soil < p[kParamTreeMinSoil]) continue;
        float maxHeight = 1.0f + p[kParamTreeSoil] * std::sqrt(soil);
        bool oak = rng.chance(oakShare * 0.5f);
        float size = oak ? rng.range(0.3f, 0.8f) : rng.range(0.4f, 1.1f);
        float crowding = estimateCrowding(bvh, s.point, trunks);
        Rng treeRng = rng.fork(uint32_t(7000 + placed));
        growTree(p, treeRng, &bvh, s.point - kUp * 0.02f, size, oak ? kTreeOak : kTreeRound, crowding, out, maxHeight);
        trunks.push_back(s.point);
        ++placed;
    }

    // The ground patch: far enough from the walls for the crown, close enough to belong to the house.
    SurfaceSampler ground(bvh, kMaskGround);
    const int wantedGround = p.i(kParamTreeGroundCount);
    int placedGround = 0;
    for (int attempt = 0; !ground.empty() && attempt < wantedGround * 300 && placedGround < wantedGround; ++attempt) {
        SurfaceSample s = ground.sample(rng);
        if (s.normal.y < 0.7f) continue;
        bool oak = rng.chance(oakShare);
        float size = oak ? rng.range(0.6f, 1.0f) : rng.range(0.7f, 1.3f);
        // Crown width as if the tree were crowded (it will be, this close to a house).
        float crownR = oak ? p[kParamOakHeight] * size * 0.35f : p[kParamTreeHeight] * size * p[kParamTreeCanopy];
        SurfaceHit w;
        float d = bvh.closestPoint(s.point + kUp * 0.5f, 15.0f, w, kMaskSolid | kMaskNoGrow) ? w.distance : 15.0f;
        if (d < std::max(2.0f, crownR * 0.75f) || d > crownR * 0.75f + 5.0f) continue;
        if (!spaced(s.point, 4.0f) || isEnclosed(bvh, s.point)) continue;
        float crowding = estimateCrowding(bvh, s.point, trunks);
        Rng treeRng = rng.fork(uint32_t(8000 + placedGround));
        growTree(p, treeRng, &bvh, s.point - kUp * 0.02f, size, oak ? kTreeOak : kTreeRound, crowding, out);
        trunks.push_back(s.point);
        ++placedGround;
    }
}

}  // namespace plants
