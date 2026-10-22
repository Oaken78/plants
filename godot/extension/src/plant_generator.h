// PlantGenerator: the plant core (core/include/plants/api.h) as a Godot class.
//
// Each instance owns one PlantsContext, so separate generators can grow on separate threads at the same time
// (WorkerThreadPool). Every generate call returns one ArrayMesh per LOD (null for an empty LOD) in the core's own
// 24-byte vertex layout:
//   VERTEX   float x3
//   CUSTOM0  RGBA8 unorm: hue shift, value, ambient occlusion, leaf v (255 = not a leaf)
//   CUSTOM1  RGBA8 unorm: wind flex, wind phase, birth, material id
//   CUSTOM2  RGBA8 snorm: smooth shading normal xyz, leaf u
// There is no ARRAY_NORMAL: the foliage shader reads the normal from CUSTOM2, which keeps the leaf coordinate in
// its fourth byte instead of needing a fourth stream.
#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/typed_array.hpp>

struct PlantsContext;

namespace godot {

class PlantGenerator : public RefCounted {
    GDCLASS(PlantGenerator, RefCounted)

public:
    enum SurfaceTag { TAG_SOLID = 0, TAG_GROUND = 1, TAG_NO_GROW = 2 };

    PlantGenerator();
    ~PlantGenerator() override;

    static TypedArray<Dictionary> get_param_list();
    void set_param(const String& name, float value);
    float get_param(const String& name) const;
    void reset_params();
    void set_params(const Dictionary& overrides);

    void set_host(const PackedVector3Array& positions, const PackedInt32Array& indices, const PackedByteArray& tags);

    Array grow_on_host(int64_t seed);
    Array make_bush(int64_t seed, int style, float scale);
    Array make_tree(int64_t seed, float size, int species, float crowding);
    Array make_grass(int64_t seed);
    // A coarser LOD 3 of the plants grown last (plants_rebuild_far_lod): clumps merged per merge_cell metres,
    // stems thinner than min_stem_radius dropped.
    Ref<ArrayMesh> build_far_lod(float merge_cell, float min_stem_radius);

    Dictionary get_stats() const;
    double get_last_generate_msec() const { return last_msec_; }

protected:
    static void _bind_methods();

private:
    PlantsContext* ctx_ = nullptr;
    double last_msec_ = 0.0;

    Array collect();
    Ref<ArrayMesh> build_mesh(int lod) const;
};

}  // namespace godot

VARIANT_ENUM_CAST(PlantGenerator::SurfaceTag);
