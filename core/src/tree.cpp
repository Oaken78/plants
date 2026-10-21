// Trees.
//
// The crown comes first: overlapping clumps in a wide rounded envelope (the same cloud-shaded foliage as the
// bushes, so trees read as soft anime masses). The skeleton is then built to carry it: a slightly curved trunk to a
// fork, a few limbs that each serve the clumps in one direction, a branch into every clump and twigs inside it.
// Branch thickness follows the pipe model (radius ~ sqrt of the foliage a branch carries).
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

    // A gently curved bark branch from an existing node to `to`; returns its end node.
    int branch(int from, const Vec3& to, int segments, const Vec3& bend, Rng& rng) {
        Stem stem;
        stem.mat = kMatBark;
        stem.nodes.push_back(from);
        const Vec3 a = out.nodes[from].pos;
        const float len = length(to - a);
        int prev = from;
        for (int s = 1; s <= segments; ++s) {
            float t = float(s) / segments;
            Vec3 pos = lerp(a, to, t) + bend * std::sin(t * kPi);
            if (s < segments) pos += rng.inUnitSphere() * (len * 0.03f);
            prev = add(pos, kUp, prev);
            stem.nodes.push_back(prev);
        }
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

int growTree(const Params& p, Rng& rng, const TriangleBvh* host, const Vec3& base, float size, PlantSet& out) {
    const int group = out.nextGroup++;
    ++out.treeCount;
    float H = p[kParamTreeHeight] * size * rng.range(0.85f, 1.15f);
    if (host) {
        // Fit under overhangs.
        SurfaceHit h;
        if (host->raycast(base + kUp * 0.3f, kUp, H * 1.15f, h, kMaskSolid | kMaskNoGrow))
            H = std::max(1.2f, (h.distance + 0.3f) * 0.85f);
    }
    const float sizeFactor = H / 7.0f;
    Vec3 lean = openSky(host, base, std::max(3.0f, H * 0.6f)) * p[kParamTreeLean];
    if (length(lean) > 0.45f) lean = normalize(lean) * 0.45f;
    const Vec3 axis = normalize(kUp + lean + projectOnPlane(rng.unitVector(), kUp) * 0.08f);
    const Frame fr = makeFrame(axis, anyPerpendicular(axis));
    const float R = H * p[kParamTreeCanopy] * rng.range(0.85f, 1.15f);
    const float Rv = R * 0.75f;
    const float trunkR = std::max(0.03f, H * p[kParamTreeTrunk] * rng.range(0.85f, 1.15f));
    const float forkH = H * rng.range(0.26f, 0.38f);
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
        c.mat = kMatTreeLeaf;
        c.hue = hue + rng.range(-0.15f, 0.15f);
        c.value = value;
        c.flex = (0.03f + 0.05f * saturate(dot(pos - base, axis) / H)) * flexScale;
        c.phase = phase + rng.range(0.0f, 0.2f);
        out.clumps.push_back(c);
    };
    addClump(crown + axis * (Rv * 0.42f), R * 0.52f);
    int clumpCount = int(p[kParamTreeClumps] * (0.55f + 0.45f * size) + 0.5f);
    clumpCount = std::max(4, std::min(26, clumpCount));
    float ang = rng.range(0.0f, 2 * kPi);
    for (int i = 1; i < clumpCount; ++i) {
        ang += 2.39996f + rng.range(-0.35f, 0.35f);
        float elev = rng.range(-0.55f, 0.65f);
        float r = R * rng.range(0.34f, 0.5f);
        Vec3 pos = crown + fr.side * (std::cos(ang) * std::cos(elev) * R * 0.7f) + fr.normal * (std::sin(elev) * Rv * 0.72f) +
                   fr.forward * (std::sin(ang) * std::cos(elev) * R * 0.7f);
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
    for (int s = 1; s <= 6; ++s) {
        float t = s / 6.0f;
        Vec3 wobble = (fr.side * std::cos(phi + t * 2.2f) + fr.forward * std::sin(phi + t * 2.2f)) * (H * 0.025f * std::sin(t * kPi));
        prev = sk.add(base + axis * (forkH * t) + wobble, axis, prev);
        trunk.nodes.push_back(prev);
    }
    out.stems.push_back(trunk);
    const int fork = prev;
    const Vec3 forkPos = out.nodes[fork].pos;

    auto twigs = [&](int from, const Clump& c) {
        for (int k = 0; k < 3; ++k) {
            Vec3 d = normalize(rng.unitVector() + kUp * 0.5f);
            int e = sk.branch(from, c.center + d * (c.radii.x * 0.8f), 2, Vec3{}, rng);
            sk.w(e) += c.radii.x * c.radii.x * 0.05f;
        }
    };
    // Limbs: ring clumps sorted by direction around the trunk, split into a few contiguous groups.
    std::vector<int> ring;
    for (size_t ci = firstClump + 1; ci < out.clumps.size(); ++ci) ring.push_back(int(ci));
    auto azimuth = [&](int ci) {
        Vec3 d = out.clumps[ci].center - forkPos;
        return std::atan2(dot(d, fr.forward), dot(d, fr.side));
    };
    std::sort(ring.begin(), ring.end(), [&](int a, int b) { return azimuth(a) < azimuth(b); });
    std::rotate(ring.begin(), ring.begin() + rng.rangeInt(0, int(ring.size()) - 1), ring.end());
    int limbs = std::max(1, std::min(std::min(5, int(ring.size())), int(ring.size() / 3.5f + 0.5f)));
    for (int l = 0; l < limbs; ++l) {
        size_t b0 = ring.size() * l / limbs, b1 = ring.size() * (l + 1) / limbs;
        Vec3 centroid;
        for (size_t i = b0; i < b1; ++i) centroid += out.clumps[ring[i]].center;
        centroid = centroid / float(b1 - b0);
        Vec3 limbEnd = lerp(forkPos, centroid, 0.6f) + axis * (R * 0.08f);
        int limb = sk.branch(fork, limbEnd, 4, axis * (R * 0.12f), rng);
        for (size_t i = b0; i < b1; ++i) {
            const Clump c = out.clumps[ring[i]];
            int e = sk.branch(limb, lerp(limbEnd, c.center, 0.88f), 3, axis * (c.radii.x * 0.15f), rng);
            sk.w(e) += c.radii.x * c.radii.x;
            twigs(e, c);
        }
    }
    {
        const Clump c = out.clumps[firstClump];
        int e = sk.branch(fork, lerp(forkPos, c.center, 0.9f), 4, Vec3{}, rng);
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
    for (size_t i = sk.first; i < end; ++i) {
        StemNode& node = out.nodes[i];
        float h = dot(node.pos - base, axis);
        node.radius = std::max(0.006f, trunkR * std::sqrt(sk.weight[i - sk.first] / total));
        if (h < H * 0.12f) node.radius *= 1.0f + 0.6f * (1.0f - h / (H * 0.12f)) * (1.0f - h / (H * 0.12f));
        float t = sk.path[i - sk.first] / maxPath;
        node.birth = 0.05f + 0.5f * t;
        node.flex = 0.07f * t * t * flexScale;
    }

    // Foliage on the crown.
    float crownBottom = H;
    for (size_t ci = firstClump; ci < out.clumps.size(); ++ci)
        crownBottom = std::min(crownBottom, dot(out.clumps[ci].center - base, axis) - out.clumps[ci].radii.x * 0.9f);
    const size_t firstLeaf = out.leaves.size();
    coverClumpsWithLeaves(rng, out, firstClump, base, axis, crownBottom, H,
                          p[kParamTreeLeafSize] * (0.8f + 0.3f * std::min(size, 2.0f)), p[kParamTreeLeafDensity],
                          kMatTreeLeaf, kShapeBush, group, 0.55f);
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

void scatterTreesOnHost(const TriangleBvh& bvh, const Params& p, Rng& rng, PlantSet& out, std::vector<Vec3>& trunks) {
    const float floorY = bvh.bounds().lo.y;
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
        if (!spaced(s.point, 3.5f)) continue;
        Rng treeRng = rng.fork(uint32_t(7000 + placed));
        growTree(p, treeRng, &bvh, s.point - kUp * 0.02f, rng.range(0.35f, 0.75f), out);
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
        float size = rng.range(0.7f, 1.3f);
        float crownR = p[kParamTreeHeight] * size * p[kParamTreeCanopy];
        SurfaceHit w;
        float d = bvh.closestPoint(s.point + kUp * 0.5f, 15.0f, w, kMaskSolid | kMaskNoGrow) ? w.distance : 15.0f;
        if (d < std::max(2.0f, crownR * 0.75f) || d > crownR * 0.75f + 5.0f) continue;
        if (!spaced(s.point, 4.0f)) continue;
        Rng treeRng = rng.fork(uint32_t(8000 + placedGround));
        growTree(p, treeRng, &bvh, s.point - kUp * 0.02f, size, out);
        trunks.push_back(s.point);
        ++placedGround;
    }
}

}  // namespace plants
