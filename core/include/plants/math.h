// Small, dependency-free vector math for the plant core.
// Kept header-only so the core compiles unchanged for WebAssembly and for a Godot GDExtension.
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace plants {

constexpr float kPi = 3.14159265358979f;

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(const Vec3& b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& b) { x += b.x; y += b.y; z += b.z; return *this; }
    Vec3& operator-=(const Vec3& b) { x -= b.x; y -= b.y; z -= b.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length2(const Vec3& v) { return dot(v, v); }
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v, const Vec3& fallback = {0, 1, 0}) {
    float l = length(v);
    return l > 1e-8f ? v / l : fallback;
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 vmin(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 vmax(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline Vec3 projectOnPlane(const Vec3& v, const Vec3& n) { return v - n * dot(v, n); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float e0, float e1, float x) {
    float t = saturate((x - e0) / (e1 - e0));
    return t * t * (3 - 2 * t);
}

// Rotate v around unit axis by angle (Rodrigues).
inline Vec3 rotateAxis(const Vec3& v, const Vec3& axis, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1 - c));
}

// Any unit vector perpendicular to n.
inline Vec3 anyPerpendicular(const Vec3& n) {
    Vec3 a = std::fabs(n.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    return normalize(cross(a, n));
}

// Orthonormal frame: x = side, y = forward (along length), z = normal.
struct Frame {
    Vec3 side, forward, normal;
    Vec3 apply(float sx, float fy, float nz) const { return side * sx + forward * fy + normal * nz; }
};

// Builds a frame whose normal is n and whose forward is as close as possible to f.
inline Frame makeFrame(const Vec3& n, const Vec3& f) {
    Frame fr;
    fr.normal = normalize(n);
    Vec3 fw = projectOnPlane(f, fr.normal);
    if (length2(fw) < 1e-8f) fw = anyPerpendicular(fr.normal);
    fr.forward = normalize(fw);
    fr.side = cross(fr.forward, fr.normal);
    return fr;
}

struct Aabb {
    Vec3 lo{1e30f, 1e30f, 1e30f};
    Vec3 hi{-1e30f, -1e30f, -1e30f};
    void grow(const Vec3& p) { lo = vmin(lo, p); hi = vmax(hi, p); }
    void grow(const Aabb& b) { lo = vmin(lo, b.lo); hi = vmax(hi, b.hi); }
    Vec3 center() const { return (lo + hi) * 0.5f; }
    Vec3 extent() const { return hi - lo; }
    bool valid() const { return lo.x <= hi.x; }
    float distance2(const Vec3& p) const {
        float dx = std::max(std::max(lo.x - p.x, 0.0f), p.x - hi.x);
        float dy = std::max(std::max(lo.y - p.y, 0.0f), p.y - hi.y);
        float dz = std::max(std::max(lo.z - p.z, 0.0f), p.z - hi.z);
        return dx * dx + dy * dy + dz * dz;
    }
};

// Cheap value noise used to add organic variation (lumps on clumps, colour patches).
inline float hash3(int x, int y, int z) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u +
                 static_cast<uint32_t>(z) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xffffff) / float(0xffffff);
}

inline float valueNoise(const Vec3& p) {
    int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y), iz = (int)std::floor(p.z);
    float fx = p.x - ix, fy = p.y - iy, fz = p.z - iz;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
    float v000 = hash3(ix, iy, iz), v100 = hash3(ix + 1, iy, iz);
    float v010 = hash3(ix, iy + 1, iz), v110 = hash3(ix + 1, iy + 1, iz);
    float v001 = hash3(ix, iy, iz + 1), v101 = hash3(ix + 1, iy, iz + 1);
    float v011 = hash3(ix, iy + 1, iz + 1), v111 = hash3(ix + 1, iy + 1, iz + 1);
    float a = lerpf(lerpf(v000, v100, fx), lerpf(v010, v110, fx), fy);
    float b = lerpf(lerpf(v001, v101, fx), lerpf(v011, v111, fx), fy);
    return lerpf(a, b, fz);
}

}  // namespace plants
