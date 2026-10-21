#include "plants/bvh.h"

namespace plants {

// Ericson, Real-Time Collision Detection 5.1.5.
Vec3 closestPointOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    Vec3 bp = p - b;
    float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    Vec3 cp = p - c;
    float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

void TriangleBvh::build(const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices,
                        const std::vector<uint8_t>& tags) {
    tris_.clear();
    nodes_.clear();
    order_.clear();
    centroids_.clear();
    size_t triCount = indices.size() / 3;
    tris_.reserve(triCount);
    for (size_t t = 0; t < triCount; ++t) {
        Tri tri;
        tri.a = positions[indices[t * 3]];
        tri.b = positions[indices[t * 3 + 1]];
        tri.c = positions[indices[t * 3 + 2]];
        Vec3 n = cross(tri.b - tri.a, tri.c - tri.a);
        float len = length(n);
        if (len < 1e-10f) continue;  // skip degenerate triangles
        tri.normal = n / len;
        tri.area = 0.5f * len;
        tri.tag = t < tags.size() ? tags[t] : kTagSolid;
        tris_.push_back(tri);
    }
    if (tris_.empty()) return;
    order_.resize(tris_.size());
    centroids_.resize(tris_.size());
    for (size_t i = 0; i < tris_.size(); ++i) {
        order_[i] = int(i);
        centroids_[i] = (tris_[i].a + tris_[i].b + tris_[i].c) / 3.0f;
    }
    nodes_.reserve(tris_.size() * 2);
    buildRecursive(0, int(tris_.size()), 0);
    // Reorder triangles so leaves reference contiguous ranges.
    std::vector<Tri> sorted(tris_.size());
    for (size_t i = 0; i < order_.size(); ++i) sorted[i] = tris_[order_[i]];
    tris_.swap(sorted);
    centroids_.clear();
    centroids_.shrink_to_fit();
}

int TriangleBvh::buildRecursive(int first, int count, int depth) {
    int index = int(nodes_.size());
    nodes_.push_back(Node{});
    Aabb box, cbox;
    for (int i = first; i < first + count; ++i) {
        const Tri& t = tris_[order_[i]];
        box.grow(t.a); box.grow(t.b); box.grow(t.c);
        cbox.grow(centroids_[order_[i]]);
    }
    nodes_[index].box = box;
    if (count <= 4 || depth > 40) {
        nodes_[index].left = first;
        nodes_[index].count = count;
        return index;
    }
    Vec3 ext = cbox.extent();
    int axis = (ext.x > ext.y && ext.x > ext.z) ? 0 : (ext.y > ext.z ? 1 : 2);
    int mid = first + count / 2;
    std::nth_element(order_.begin() + first, order_.begin() + mid, order_.begin() + first + count,
                     [&](int a, int b) { return centroids_[a][axis] < centroids_[b][axis]; });
    int l = buildRecursive(first, mid - first, depth + 1);
    int r = buildRecursive(mid, first + count - mid, depth + 1);
    nodes_[index].left = l;
    nodes_[index].right = r;
    nodes_[index].count = 0;
    return index;
}

bool TriangleBvh::closestPoint(const Vec3& p, float maxDist, SurfaceHit& out, uint32_t mask) const {
    if (nodes_.empty()) return false;
    float best2 = maxDist * maxDist;
    int bestTri = -1;
    Vec3 bestPoint;
    int stack[96];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& n = nodes_[stack[--sp]];
        if (n.box.distance2(p) > best2) continue;
        if (n.count > 0) {
            for (int i = n.left; i < n.left + n.count; ++i) {
                const Tri& t = tris_[i];
                if (!((1u << t.tag) & mask)) continue;
                Vec3 q = closestPointOnTriangle(p, t.a, t.b, t.c);
                float d2 = length2(q - p);
                if (d2 < best2) { best2 = d2; bestTri = i; bestPoint = q; }
            }
        } else if (sp < 94) {
            // Push the farther child first so the nearer one is searched first.
            float dl = nodes_[n.left].box.distance2(p);
            float dr = nodes_[n.right].box.distance2(p);
            if (dl < dr) { stack[sp++] = n.right; stack[sp++] = n.left; }
            else { stack[sp++] = n.left; stack[sp++] = n.right; }
        }
    }
    if (bestTri < 0) return false;
    out.point = bestPoint;
    out.normal = tris_[bestTri].normal;
    out.distance = std::sqrt(best2);
    out.triangle = bestTri;
    out.tag = tris_[bestTri].tag;
    return true;
}

static bool rayBox(const Aabb& b, const Vec3& o, const Vec3& invD, float maxT) {
    float t0 = 0, t1 = maxT;
    for (int a = 0; a < 3; ++a) {
        float tn = (b.lo[a] - o[a]) * invD[a];
        float tf = (b.hi[a] - o[a]) * invD[a];
        if (tn > tf) std::swap(tn, tf);
        t0 = std::max(t0, tn);
        t1 = std::min(t1, tf);
        if (t0 > t1) return false;
    }
    return true;
}

bool TriangleBvh::raycast(const Vec3& origin, const Vec3& dir, float maxT, SurfaceHit& out, uint32_t mask) const {
    if (nodes_.empty()) return false;
    Vec3 invD{1.0f / (std::fabs(dir.x) > 1e-12f ? dir.x : 1e-12f), 1.0f / (std::fabs(dir.y) > 1e-12f ? dir.y : 1e-12f),
              1.0f / (std::fabs(dir.z) > 1e-12f ? dir.z : 1e-12f)};
    float bestT = maxT;
    int bestTri = -1;
    int stack[96];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& n = nodes_[stack[--sp]];
        if (!rayBox(n.box, origin, invD, bestT)) continue;
        if (n.count > 0) {
            for (int i = n.left; i < n.left + n.count; ++i) {
                const Tri& t = tris_[i];
                if (!((1u << t.tag) & mask)) continue;
                // Moller-Trumbore, two-sided.
                Vec3 e1 = t.b - t.a, e2 = t.c - t.a;
                Vec3 pv = cross(dir, e2);
                float det = dot(e1, pv);
                if (std::fabs(det) < 1e-12f) continue;
                float inv = 1.0f / det;
                Vec3 tv = origin - t.a;
                float u = dot(tv, pv) * inv;
                if (u < 0 || u > 1) continue;
                Vec3 qv = cross(tv, e1);
                float v = dot(dir, qv) * inv;
                if (v < 0 || u + v > 1) continue;
                float tt = dot(e2, qv) * inv;
                if (tt > 1e-5f && tt < bestT) { bestT = tt; bestTri = i; }
            }
        } else if (sp < 94) {
            stack[sp++] = n.left;
            stack[sp++] = n.right;
        }
    }
    if (bestTri < 0) return false;
    out.point = origin + dir * bestT;
    out.normal = tris_[bestTri].normal;
    out.distance = bestT;
    out.triangle = bestTri;
    out.tag = tris_[bestTri].tag;
    return true;
}

}  // namespace plants
