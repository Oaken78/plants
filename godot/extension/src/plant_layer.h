// PlantLayer: streaming, cell-based LOD instancing (the native port of web/js/instancer.js).
//
// The layer covers the world with square cells that DemoWorld fills lazily from deterministic rules. Every frame,
// update() walks the cells within range that are in view (or whose shadows can reach the view), picks a LOD per
// instance and part from its distance, and appends it to that (variant, part, LOD) bucket. Each bucket is one
// RenderingServer MultiMesh, so one draw call. Near a LOD switch an instance goes into both buckets with
// complementary dither codes (INSTANCE_CUSTOM.x, see lod_dither() in shaders/common.gdshaderinc), which
// cross-fades without popping or transparency sorting.
//
// update_layers() walks several layers at once on Godot's WorkerThreadPool; only the uploads to the RenderingServer
// run on the main thread.
//
// Why not Godot's visibility ranges: they measure distance per node, so they would need one node per instance per
// LOD (thousands of draw calls) or cells so small that the far LODs turn into tens of thousands of nodes. Choosing
// LODs per instance here keeps every LOD level a single draw call, and costs well under a millisecond per frame.
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/node3d.hpp>

#include "demo_world.h"

namespace godot {

class PlantLayer : public Node3D {
    GDCLASS(PlantLayer, Node3D)

public:
    ~PlantLayer() override;

    void configure(const Ref<DemoWorld>& world, int kind, float cell_size, float max_distance, float y_min, float y_max,
                   float fade);
    void set_variant(int index, const Array& parts);
    void clear_variants();
    void update(Camera3D* camera, const Vector3& sun_direction, float shadow_distance);
    // Updates all layers for this frame, walking them in parallel. Returns the wall time in microseconds.
    static double update_layers(const TypedArray<PlantLayer>& layers, Camera3D* camera, const Vector3& sun_direction,
                                float shadow_distance);
    Dictionary get_stats() const;

protected:
    static void _bind_methods();
    void _notification(int what);

private:
    struct Bucket {
        RID multimesh, instance;
        // Whether this bucket draws anything. RID::is_valid() is a call into the engine, too slow for the per-instance
        // loop, so update() reads this instead.
        bool live = false;
        Ref<Mesh> mesh;
        int lod = 0;
        int capacity = 0;       // instances the staging vector holds
        int allocated = 0;      // instances the multimesh holds; grown on the main thread in commit()
        int count = 0;
        bool shown = false;
        int64_t triangles = 0;
        Vector3 local_center;   // centre and radius of the mesh bounds, for the bucket's AABB
        float local_radius = 0;
        // Instances are written to a plain vector and copied into the packed array once per frame: every ptrw() on
        // a packed array is a call into the engine with a copy-on-write check.
        std::vector<float> staging;
        PackedFloat32Array data;
        Vector3 lo, hi;         // bounds of this frame's instances
    };
    struct Part {
        std::vector<float> ranges;      // LOD i is used while distance < ranges[i]
        std::vector<Bucket> buckets;    // one per LOD; a bucket without a mesh is never drawn
        // Shadow-casting LODs get a second bucket for instances beyond the shadow distance, drawn without shadows.
        // Otherwise one bucket spans near and far instances, its bounds reach every cascade, and the whole town is
        // drawn into all of them.
        std::vector<Bucket> far;
        // Shadow proxies: a LOD can cast its shadow with a coarser mesh. It then casts nothing itself, and its near
        // instances also go into proxy[lod], a shadows-only bucket with that mesh. LOD 0 casting with LOD 1, which
        // has a third of the leaves enlarged to the same coverage, looks the same at a fraction of the cost: leaves
        // shade one another by clump anyway (see the shadow bias in main.gd).
        std::vector<Bucket> proxy;
    };
    struct PlantVariant {
        std::vector<Part> parts;
    };
    struct Cell {
        std::vector<overgrowth::Instance> instances;
        float y0 = 0, y1 = 0;
    };
    // Cells are kept in blocks of kBlock x kBlock: one hash lookup and one frustum test per block, so the many
    // cells out of view cost nothing. A cell is generated the first time its block is in range and in view.
    static constexpr int kBlock = 8;
    struct Block {
        Cell cells[kBlock * kBlock];
        uint64_t generated = 0;  // bit per cell
        float y0 = 1e30f, y1 = -1e30f;
    };

    Ref<DemoWorld> world_;
    int kind_ = overgrowth::kCellBushes;
    float cell_size_ = 32;
    float max_distance_ = 1000;
    float y_min_ = -50, y_max_ = 150;  // vertical extent of an instance above its base, for cell culling
    float fade_ = 0.12f;               // cross-fade band as a fraction of the switch distance
    std::vector<PlantVariant> variants_;
    std::unordered_map<int64_t, std::unique_ptr<Block>> blocks_;
    RID scenario_;

    struct Stats {
        int instances = 0;
        int per_lod[5] = {0, 0, 0, 0, 0};
        int64_t triangles = 0;
        int draws = 0;
        int cells = 0;
        int pushes = 0;
        double usec = 0;
        double upload_usec = 0;
        double walk_usec = 0;  // on whichever thread walked this layer
    } stats_;

    Block& block(int bx, int bz);
    const Cell& cell(Block& b, int cx, int cz);

    // What the walk needs from the camera, gathered once per frame on the main thread.
    struct View {
        std::vector<Plane> planes;
        Vector3 cam;
        Vector3 away_from_sun;
        float shadow_distance = 0;
    };
    bool begin();                 // main thread: reset the buckets; false if the layer is hidden
    void walk(const View& view);  // any thread: touches only this layer's cells and buckets
    void finish();                // main thread: upload
    static void walk_task(int index);
    void push(Bucket& b, const overgrowth::Instance& inst, float fade_code);
    void commit(Bucket& b);
    void free_variant(PlantVariant& v);
    template <typename Fn>
    void for_each_bucket(Fn&& fn) {
        for (PlantVariant& v : variants_)
            for (Part& p : v.parts) {
                for (Bucket& b : p.buckets) fn(b);
                for (Bucket& b : p.far) fn(b);
                for (Bucket& b : p.proxy) fn(b);
            }
    }
    void apply_visibility();
};

}  // namespace godot
