// Triangle BVH over the host model: closest-point and ray queries that let plants cling to any mesh.
#pragma once

#include <vector>
#include "math.h"

namespace plants {

// Per-triangle surface tags. Hosts pass these so plants know where they may grow.
enum SurfaceTag : uint8_t {
    kTagSolid = 0,    // walls, roofs, ledges: climbers and bushes may grow here
    kTagGround = 1,   // terrain patch around the model: bushes and climber roots
    kTagNoGrow = 2,   // glass, doors, anything plants must avoid
};

// Bit masks for queries.
enum TagMask : uint32_t {
    kMaskSolid = 1u << kTagSolid,
    kMaskGround = 1u << kTagGround,
    kMaskNoGrow = 1u << kTagNoGrow,
    kMaskAll = 0xffffffffu,
};

struct SurfaceHit {
    Vec3 point;
    Vec3 normal;     // geometric face normal
    float distance = 0;
    int triangle = -1;
    uint8_t tag = 0;
};

class TriangleBvh {
public:
    void build(const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices,
               const std::vector<uint8_t>& tags);
    bool empty() const { return tris_.empty(); }

    // Closest surface point within maxDist whose tag is in mask.
    bool closestPoint(const Vec3& p, float maxDist, SurfaceHit& out, uint32_t mask = kMaskAll) const;
    // First hit along origin + dir * t, t in (0, maxT]. dir must be normalized.
    bool raycast(const Vec3& origin, const Vec3& dir, float maxT, SurfaceHit& out, uint32_t mask = kMaskAll) const;

    const Aabb& bounds() const { return nodes_.empty() ? empty_ : nodes_[0].box; }
    int triangleCount() const { return int(tris_.size()); }

    struct Tri {
        Vec3 a, b, c;
        Vec3 normal;
        float area;
        uint8_t tag;
    };
    const Tri& triangle(int i) const { return tris_[i]; }

private:
    struct Node {
        Aabb box;
        int left = -1;   // left child index, or first triangle when leaf
        int right = -1;  // right child index
        int count = 0;   // >0 means leaf
    };
    int buildRecursive(int first, int count, int depth);

    std::vector<Tri> tris_;
    std::vector<int> order_;
    std::vector<Node> nodes_;
    std::vector<Vec3> centroids_;
    Aabb empty_;
};

Vec3 closestPointOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c);

}  // namespace plants
