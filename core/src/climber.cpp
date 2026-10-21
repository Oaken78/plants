// Climbing plants (ivy).
//
// Every growing tip advances one step per round, steered by:
//   - its previous direction (smooth curves),
//   - noise (wandering),
//   - "up" projected onto the surface (climbing),
//   - adhesion toward the closest host surface (clinging),
//   - gravity once it loses contact.
// Tips slide along anything they hit, branch at random, avoid crowded areas, and either
// die or turn into hanging strands when they fall off an edge.
#include <unordered_map>
#include "plants/generator.h"

namespace plants {

namespace {

const Vec3 kUp{0, 1, 0};
const Vec3 kDown{0, -1, 0};
const float kOffset = 0.03f;          // how far stems sit off the surface
const float kHangFlag = 0.02f;        // node.flex above this marks a hanging strand

struct Tip {
    int stem = 0;
    Vec3 dir;
    float floatLen = 0;   // distance travelled without touching a surface
    float groundLen = 0;  // distance crept along the ground
    int depth = 0;
    int stepsLeft = 0;
    bool hanging = false;
    float hangLeft = 0, hangTotal = 0;
    bool alive = true;
};

// Sparse occupancy grid that keeps branches from piling on top of each other.
class Occupancy {
public:
    explicit Occupancy(float cell) : cell_(cell) {}
    int count(const Vec3& p) const {
        auto it = map_.find(key(p));
        return it == map_.end() ? 0 : it->second;
    }
    void add(const Vec3& p) { ++map_[key(p)]; }

private:
    uint64_t key(const Vec3& p) const {
        auto c = [&](float v) { return uint64_t(uint32_t(int(std::floor(v / cell_)) + (1 << 20)) & 0x1fffff); };
        return (c(p.x) << 42) | (c(p.y) << 21) | c(p.z);
    }
    float cell_;
    std::unordered_map<uint64_t, int> map_;
};

uint64_t cellKey(const Vec3& p, float cell) {
    auto c = [&](float v) { return uint64_t(uint32_t(int(std::floor(v / cell)) + (1 << 20)) & 0x1fffff); };
    return (c(p.x) << 42) | (c(p.y) << 21) | c(p.z);
}

}  // namespace

void growClimbers(const TriangleBvh& bvh, const Params& p, Rng& rng, const std::vector<ClimberSeed>& seeds,
                  PlantSet& out) {
    if (bvh.empty() || seeds.empty()) return;
    const float step = p[kParamClimberStep];
    const float adhesionDist = p[kParamClimberAdhesionDist];
    const float wUp = p[kParamClimberUp];
    const float wRand = p[kParamClimberRandom];
    const float wAdh = p[kParamClimberAdhesion];
    const float maxFloat = p[kParamClimberMaxFloat];
    const float hangChance = p[kParamClimberHangChance];
    const float hangLength = p[kParamClimberHangLength];
    const int budget = p.i(kParamClimberBudget);
    const int maxSteps = p.i(kParamClimberMaxSteps);
    const uint32_t stickMask = kMaskSolid | kMaskGround;

    const size_t firstNode = out.nodes.size();
    const size_t firstStem = out.stems.size();
    std::vector<Tip> tips;
    Occupancy occupancy(step * 1.6f);

    auto addNode = [&](const Vec3& pos, const Vec3& n, int parent, float birth, float flex) {
        StemNode node;
        node.pos = pos;
        node.normal = n;
        node.parent = parent;
        node.birth = birth;
        node.flex = flex;
        out.nodes.push_back(node);
        return int(out.nodes.size() - 1);
    };
    auto startHanging = [&](Tip& t) {
        t.hanging = true;
        t.hangTotal = t.hangLeft = hangLength * rng.range(0.3f, 1.2f);
    };

    for (const auto& s : seeds) {
        int node = addNode(s.point, s.normal, -1, 0, 0.003f);
        out.stems.push_back(Stem{{node}});
        Tip t;
        t.stem = int(out.stems.size() - 1);
        Vec3 d = s.onTop ? projectOnPlane(rng.unitVector(), s.normal) : projectOnPlane(kUp, s.normal) + rng.unitVector() * 0.2f;
        t.dir = normalize(projectOnPlane(d, s.normal), anyPerpendicular(s.normal));
        t.stepsLeft = maxSteps;
        tips.push_back(t);
        occupancy.add(s.point);
        ++out.climberCount;
    }

    int nodeCount = 0;
    int round = 0;
    const int maxRounds = maxSteps * 3;
    while (nodeCount < budget && round < maxRounds) {
        bool any = false;
        const size_t tipCount = tips.size();  // branches spawned this round start growing next round
        for (size_t ti = 0; ti < tipCount && nodeCount < budget; ++ti) {
            Tip t = tips[ti];
            if (!t.alive) continue;
            any = true;
            const int cur = out.stems[t.stem].nodes.back();
            const Vec3 pos = out.nodes[cur].pos;
            const Vec3 curNormal = out.nodes[cur].normal;
            const Vec3 rnd = rng.unitVector();
            Vec3 newPos, newNormal;
            float flex = 0.003f;

            if (t.hanging) {
                // Hanging strand: falls with a little sway and keeps clear of the wall behind it.
                Vec3 d = normalize(Vec3{t.dir.x * 0.25f + rnd.x * 0.15f, -1.0f, t.dir.z * 0.25f + rnd.z * 0.15f});
                Vec3 outward = curNormal;
                SurfaceHit nearHit;
                if (bvh.closestPoint(pos, 0.16f, nearHit, kMaskAll)) {
                    Vec3 away = normalize(pos - nearHit.point, nearHit.normal);
                    d = normalize(d + away * ((0.16f - nearHit.distance) * 6.0f));
                    outward = away;
                }
                SurfaceHit hit;
                if (bvh.raycast(pos, d, step + 0.02f, hit, kMaskAll)) {
                    if (hit.normal.y > 0.5f) {  // reached the ground or a ledge below
                        tips[ti].alive = false;
                        continue;
                    }
                    d = normalize(projectOnPlane(d, hit.normal), kDown);
                }
                newPos = pos + d * step;
                newNormal = normalize(projectOnPlane(outward, kUp), outward);
                t.dir = d;
                t.hangLeft -= step;
                if (t.hangLeft <= 0) t.alive = false;
                flex = std::min(0.22f, kHangFlag + 0.01f + (t.hangTotal - t.hangLeft) * 0.07f);
            } else {
                SurfaceHit h;
                Vec3 d;
                Vec3 n = kUp;
                if (bvh.closestPoint(pos, adhesionDist, h, stickMask)) {
                    n = h.distance > 1e-4f ? normalize(pos - h.point, h.normal) : h.normal;
                    float pull = saturate((h.distance - kOffset) / adhesionDist);
                    d = projectOnPlane(t.dir, n) + projectOnPlane(rnd, n) * wRand + projectOnPlane(kUp, n) * wUp -
                        n * (pull * wAdh * 3.0f);
                } else {
                    d = t.dir + rnd * (wRand * 0.5f) + kDown * 0.7f;
                }
                d = normalize(d, t.dir);
                Vec3 np = pos + d * step;

                SurfaceHit hit;
                if (bvh.raycast(pos, d, step + kOffset, hit, kMaskAll)) {
                    Vec3 slide = projectOnPlane(d, hit.normal);
                    if (length2(slide) < 1e-4f) slide = projectOnPlane(rnd, hit.normal);
                    slide = normalize(slide, anyPerpendicular(hit.normal));
                    np = hit.point + hit.normal * kOffset + slide * (step * 0.5f);
                    d = slide;
                }

                bool attached = false;
                Vec3 nn = n;
                SurfaceHit sh;
                if (bvh.closestPoint(np, adhesionDist, sh, stickMask)) {
                    nn = sh.distance > 1e-4f ? normalize(np - sh.point, sh.normal) : sh.normal;
                    if (sh.distance < kOffset * 4) {
                        np = sh.point + nn * kOffset;
                        attached = true;
                    } else if (sh.distance < adhesionDist * 0.5f) {
                        np = lerp(np, sh.point + nn * kOffset, 0.35f);
                    }
                    if (attached && sh.tag == kTagGround) {
                        t.groundLen += step;
                        if (t.groundLen > 0.8f) t.alive = false;  // ivy only creeps a little along the ground
                    }
                }
                // Keep off glass and doors: turn away instead of covering them.
                SurfaceHit glass;
                if (bvh.closestPoint(np, kOffset * 2.5f, glass, kMaskNoGrow)) {
                    t.dir = normalize(rotateAxis(d, nn, rng.chance(0.5f) ? 1.4f : -1.4f), d);
                    if (rng.chance(0.25f)) t.alive = false;
                    tips[ti] = t;
                    continue;
                }

                t.floatLen = attached ? 0.0f : t.floatLen + step;
                // Rolling over a top edge onto a wall: drape a curtain from the edge.
                bool wasTop = curNormal.y > 0.7f;
                bool nowSide = std::fabs(nn.y) < 0.5f;
                if (wasTop && nowSide && d.y < 0.0f && t.depth < 5 && rng.chance(hangChance)) {
                    startHanging(t);
                    np = np + nn * 0.04f;
                } else if (t.floatLen > maxFloat) {
                    if (d.y < 0.3f && t.depth < 5 && rng.chance(hangChance * 0.7f)) startHanging(t);
                    else {
                        tips[ti].alive = false;
                        continue;
                    }
                }

                if (occupancy.count(np) >= 3) {
                    // Crowded: die or turn aside so coverage spreads instead of thickening.
                    if (rng.chance(0.5f)) t.alive = false;
                    else t.dir = normalize(rotateAxis(d, nn, rng.chance(0.5f) ? 1.3f : -1.3f), d);
                    tips[ti] = t;
                    continue;
                }
                newPos = np;
                newNormal = nn;
                t.dir = d;
                if (t.hanging) flex = kHangFlag + 0.01f;
            }

            int idx = addNode(newPos, newNormal, cur, float(round), flex);
            occupancy.add(newPos);
            out.stems[t.stem].nodes.push_back(idx);
            ++nodeCount;
            if (--t.stepsLeft <= 0) t.alive = false;

            if (t.alive && !t.hanging && t.depth < 5 && rng.chance(p[kParamClimberBranchChance])) {
                Tip b;
                float angle = rng.range(0.7f, 1.6f) * (rng.chance(0.5f) ? 1.0f : -1.0f);  // wide: ivy spreads sideways
                b.dir = normalize(rotateAxis(t.dir, newNormal, angle), t.dir);
                b.depth = t.depth + 1;
                b.stepsLeft = int(t.stepsLeft * rng.range(0.4f, 0.8f)) + 8;
                out.stems.push_back(Stem{{idx}});
                b.stem = int(out.stems.size() - 1);
                tips[ti] = t;
                tips.push_back(b);
                continue;
            }
            tips[ti] = t;
        }
        if (!any) break;
        ++round;
    }

    // Growth time 0..1 and stem thickness from the number of nodes each node carries.
    const float roundScale = 1.0f / float(std::max(round, 1));
    const size_t nodeEnd = out.nodes.size();
    std::vector<float> carried(nodeEnd - firstNode, 0.0f);
    for (size_t i = nodeEnd; i-- > firstNode;) {
        int parent = out.nodes[i].parent;
        if (parent >= int(firstNode)) carried[parent - firstNode] += carried[i - firstNode] + 1.0f;
    }
    const float minR = 0.0022f, maxR = p[kParamClimberStemRadius];
    for (size_t i = firstNode; i < nodeEnd; ++i) {
        StemNode& node = out.nodes[i];
        node.birth = saturate(node.birth * roundScale);
        node.radius = minR + (maxR - minR) * saturate(std::sqrt(carried[i - firstNode] / 600.0f));
    }

    // Leaves (and the odd flower) along every new stem.
    const float leafSize = p[kParamClimberLeafSize];
    const float spacing = step * 0.5f / p[kParamClimberLeafDensity];
    const float flowerChance = p[kParamClimberFlowerChance];
    const size_t firstLeaf = out.leaves.size();
    for (size_t si = firstStem; si < out.stems.size(); ++si) {
        const Stem& stem = out.stems[si];
        float acc = rng.range(0.0f, spacing);
        float sideSign = rng.chance(0.5f) ? 1.0f : -1.0f;
        for (size_t k = 1; k < stem.nodes.size(); ++k) {
            const StemNode& a = out.nodes[stem.nodes[k - 1]];
            const StemNode& b = out.nodes[stem.nodes[k]];
            float segLen = length(b.pos - a.pos);
            if (segLen < 1e-5f) continue;
            Vec3 dir = (b.pos - a.pos) / segLen;
            acc += segLen;
            while (acc >= spacing) {
                acc -= spacing;
                float t = 1.0f - acc / segLen;
                Vec3 pos = lerp(a.pos, b.pos, t);
                Vec3 n = normalize(lerp(a.normal, b.normal, t), b.normal);
                Vec3 side = normalize(cross(n, dir), anyPerpendicular(n));
                bool hanging = b.flex > kHangFlag;
                float tipness = 1.0f - saturate(carried[stem.nodes[k] - firstNode] / 40.0f);
                sideSign = -sideSign;
                Vec3 r = rng.unitVector();

                Leaf leaf;
                leaf.pos = pos + side * (sideSign * rng.range(0.005f, 0.03f)) + n * rng.range(0.0f, 0.035f);
                leaf.normal = normalize(n + kUp * (hanging ? 0.2f : 0.35f) + r * 0.4f, n);
                Vec3 fw = n.y > 0.7f ? side * sideSign + dir * 0.4f + r * 0.3f
                                     : side * (sideSign * 0.7f) + kDown * 0.6f + dir * 0.2f + r * 0.3f;
                leaf.forward = normalize(projectOnPlane(fw, leaf.normal), anyPerpendicular(leaf.normal));
                leaf.size = leafSize * rng.range(0.72f, 1.2f) * lerpf(1.08f, 0.62f, tipness);
                leaf.birth = saturate(b.birth + 0.015f);
                leaf.flex = hanging ? b.flex + 0.01f : 0.012f;
                leaf.phase = rng.uniform();
                leaf.hue = rng.range(-0.35f, 0.35f) + tipness * 0.45f;
                leaf.value = rng.range(0.88f, 1.1f) + tipness * 0.08f;
                leaf.keep = rng.uniform();
                leaf.group = 0;
                leaf.mat = kMatIvyLeaf;
                leaf.shape = kShapeIvy;
                out.leaves.push_back(leaf);

                if (rng.chance(flowerChance)) {
                    Flower f;
                    f.pos = pos + n * 0.045f;
                    f.normal = normalize(n + kUp * 0.5f + r * 0.2f, n);
                    f.size = rng.range(0.04f, 0.055f);
                    f.birth = saturate(leaf.birth + 0.1f);
                    f.flex = leaf.flex;
                    f.phase = leaf.phase;
                    f.keep = rng.uniform();
                    f.group = 0;
                    f.mat = rng.chance(0.7f) ? kMatFlowerWhite : kMatFlowerYellow;
                    out.flowers.push_back(f);
                }
            }
        }
    }

    // Clumps: cluster the leaves on a grid. They give ivy soft, rounded shading and become
    // the puffs that stand in for it at distance.
    struct Acc {
        Vec3 sum, normal;
        float sq = 0, hue = 0, flex = 0;
        int count = 0;
    };
    const float cell = p[kParamIvyClumpCell];
    std::unordered_map<uint64_t, Acc> cells;
    for (size_t i = firstLeaf; i < out.leaves.size(); ++i) {
        const Leaf& l = out.leaves[i];
        Acc& acc = cells[cellKey(l.pos, cell)];
        acc.sum += l.pos;
        acc.normal += l.normal;
        acc.hue += l.hue;
        acc.flex = std::max(acc.flex, l.flex);
        ++acc.count;
    }
    for (size_t i = firstLeaf; i < out.leaves.size(); ++i) {
        const Leaf& l = out.leaves[i];
        Acc& acc = cells[cellKey(l.pos, cell)];
        acc.sq += length2(l.pos - acc.sum / float(acc.count));
    }
    // Deterministic order: iterate leaves, emit each cell the first time it is seen.
    std::unordered_map<uint64_t, bool> emitted;
    for (size_t i = firstLeaf; i < out.leaves.size(); ++i) {
        uint64_t key = cellKey(out.leaves[i].pos, cell);
        if (emitted.count(key)) continue;
        emitted[key] = true;
        const Acc& acc = cells[key];
        if (acc.count < 3) continue;
        Vec3 center = acc.sum / float(acc.count);
        Vec3 n = normalize(acc.normal);
        float spread = std::sqrt(acc.sq / float(acc.count));
        float r = std::max(cell * 0.55f, spread * 1.45f);
        Clump c;
        c.center = center - n * (r * 0.12f);
        c.radii = {r, r * 0.5f, r};
        c.axis = n;
        c.group = 0;
        c.mat = kMatIvyLeaf;
        c.hue = acc.hue / float(acc.count);
        c.value = 1.0f;
        c.flex = acc.flex;
        c.phase = rng.uniform();
        out.clumps.push_back(c);
    }
}

}  // namespace plants
