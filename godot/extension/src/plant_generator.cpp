#include "plant_generator.h"

#include <chrono>
#include <cstring>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "plants/api.h"

namespace godot {

namespace {

int param_id(const String& name) {
    const CharString utf8 = name.utf8();
    for (int i = 0; i < plants_param_count(); ++i)
        if (std::strcmp(plants_param_name(i), utf8.get_data()) == 0) return i;
    return -1;
}

template <typename Fn>
double timed(Fn&& fn) {
    auto t0 = std::chrono::steady_clock::now();
    fn();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

PackedByteArray bytes(const void* data, int64_t size) {
    PackedByteArray out;
    out.resize(size);
    std::memcpy(out.ptrw(), data, size_t(size));
    return out;
}

}  // namespace

PlantGenerator::PlantGenerator() : ctx_(plants_create()) {}

PlantGenerator::~PlantGenerator() { plants_destroy(ctx_); }

TypedArray<Dictionary> PlantGenerator::get_param_list() {
    TypedArray<Dictionary> list;
    for (int i = 0; i < plants_param_count(); ++i) {
        Dictionary p;
        p["name"] = String::utf8(plants_param_name(i));
        p["default"] = plants_param_default(i);
        p["min"] = plants_param_min(i);
        p["max"] = plants_param_max(i);
        list.push_back(p);
    }
    return list;
}

void PlantGenerator::set_param(const String& name, float value) {
    const int id = param_id(name);
    ERR_FAIL_COND_MSG(id < 0, "Unknown plant parameter: " + name);
    plants_set_param(ctx_, id, value);
}

float PlantGenerator::get_param(const String& name) const {
    const int id = param_id(name);
    ERR_FAIL_COND_V_MSG(id < 0, 0.0f, "Unknown plant parameter: " + name);
    return plants_get_param(ctx_, id);
}

void PlantGenerator::reset_params() { plants_reset_params(ctx_); }

void PlantGenerator::set_params(const Dictionary& overrides) {
    plants_reset_params(ctx_);
    const Array keys = overrides.keys();
    for (int64_t i = 0; i < keys.size(); ++i) set_param(keys[i], overrides[keys[i]]);
}

void PlantGenerator::set_host(const PackedVector3Array& positions, const PackedInt32Array& indices,
                              const PackedByteArray& tags) {
    ERR_FAIL_COND_MSG(indices.size() % 3 != 0, "Host indices must form triangles.");
    ERR_FAIL_COND_MSG(!tags.is_empty() && tags.size() != indices.size() / 3, "Host tags need one entry per triangle.");
    static_assert(sizeof(Vector3) == 3 * sizeof(float), "Vector3 must be three packed floats (single precision)");
    plants_set_host(ctx_, reinterpret_cast<const float*>(positions.ptr()), int(positions.size()),
                    reinterpret_cast<const uint32_t*>(indices.ptr()), int(indices.size()),
                    tags.is_empty() ? nullptr : tags.ptr());
}

Array PlantGenerator::grow_on_host(int64_t seed) {
    last_msec_ = timed([&] { plants_grow_on_host(ctx_, uint32_t(seed)); });
    return collect();
}

Array PlantGenerator::make_bush(int64_t seed, int style, float scale) {
    last_msec_ = timed([&] { plants_make_bush(ctx_, uint32_t(seed), style, scale); });
    return collect();
}

Array PlantGenerator::make_tree(int64_t seed, float size, int species, float crowding) {
    last_msec_ = timed([&] { plants_make_tree(ctx_, uint32_t(seed), size, species, crowding); });
    return collect();
}

Array PlantGenerator::make_grass(int64_t seed) {
    last_msec_ = timed([&] { plants_make_grass(ctx_, uint32_t(seed)); });
    return collect();
}

Ref<ArrayMesh> PlantGenerator::build_far_lod(float merge_cell, float min_stem_radius) {
    plants_rebuild_far_lod(ctx_, merge_cell, min_stem_radius);
    return build_mesh(plants_lod_count() - 1);
}

Dictionary PlantGenerator::get_stats() const {
    static const char* names[] = {"nodes", "stems", "leaves", "flowers", "clumps", "blades", "climbers", "bushes", "trees"};
    Dictionary d;
    for (int i = 0; i < 9; ++i) d[names[i]] = plants_stat(ctx_, i);
    PackedInt32Array tris;
    for (int lod = 0; lod < plants_lod_count(); ++lod) tris.push_back(plants_mesh_index_count(ctx_, lod) / 3);
    d["triangles"] = tris;
    d["msec"] = last_msec_;
    return d;
}

Array PlantGenerator::collect() {
    Array lods;
    for (int lod = 0; lod < plants_lod_count(); ++lod) {
        Ref<ArrayMesh> mesh = build_mesh(lod);
        lods.push_back(mesh.is_valid() ? Variant(mesh) : Variant());
    }
    return lods;
}

Ref<ArrayMesh> PlantGenerator::build_mesh(int lod) const {
    const int vc = plants_mesh_vertex_count(ctx_, lod);
    const int ic = plants_mesh_index_count(ctx_, lod);
    if (vc == 0 || ic == 0) return Ref<ArrayMesh>();

    PackedVector3Array positions;
    positions.resize(vc);
    std::memcpy(positions.ptrw(), plants_mesh_positions(ctx_, lod), size_t(vc) * 12);
    PackedInt32Array indices;
    indices.resize(ic);
    std::memcpy(indices.ptrw(), plants_mesh_indices(ctx_, lod), size_t(ic) * 4);

    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = positions;
    arrays[Mesh::ARRAY_CUSTOM0] = bytes(plants_mesh_shade(ctx_, lod), int64_t(vc) * 4);
    arrays[Mesh::ARRAY_CUSTOM1] = bytes(plants_mesh_aux(ctx_, lod), int64_t(vc) * 4);
    arrays[Mesh::ARRAY_CUSTOM2] = bytes(plants_mesh_normals(ctx_, lod), int64_t(vc) * 4);
    arrays[Mesh::ARRAY_INDEX] = indices;

    const int64_t format = (int64_t(Mesh::ARRAY_CUSTOM_RGBA8_UNORM) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT) |
                           (int64_t(Mesh::ARRAY_CUSTOM_RGBA8_UNORM) << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT) |
                           (int64_t(Mesh::ARRAY_CUSTOM_RGBA8_SNORM) << Mesh::ARRAY_FORMAT_CUSTOM2_SHIFT);
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), BitField<Mesh::ArrayFormat>(format));
    return mesh;
}

void PlantGenerator::_bind_methods() {
    ClassDB::bind_static_method("PlantGenerator", D_METHOD("get_param_list"), &PlantGenerator::get_param_list);
    ClassDB::bind_method(D_METHOD("set_param", "name", "value"), &PlantGenerator::set_param);
    ClassDB::bind_method(D_METHOD("get_param", "name"), &PlantGenerator::get_param);
    ClassDB::bind_method(D_METHOD("reset_params"), &PlantGenerator::reset_params);
    ClassDB::bind_method(D_METHOD("set_params", "overrides"), &PlantGenerator::set_params);
    ClassDB::bind_method(D_METHOD("set_host", "positions", "indices", "tags"), &PlantGenerator::set_host);
    ClassDB::bind_method(D_METHOD("grow_on_host", "seed"), &PlantGenerator::grow_on_host);
    ClassDB::bind_method(D_METHOD("make_bush", "seed", "style", "scale"), &PlantGenerator::make_bush, DEFVAL(1.0f));
    ClassDB::bind_method(D_METHOD("make_tree", "seed", "size", "species", "crowding"), &PlantGenerator::make_tree,
                         DEFVAL(0), DEFVAL(0.5f));
    ClassDB::bind_method(D_METHOD("make_grass", "seed"), &PlantGenerator::make_grass);
    ClassDB::bind_method(D_METHOD("build_far_lod", "merge_cell", "min_stem_radius"), &PlantGenerator::build_far_lod);
    ClassDB::bind_method(D_METHOD("get_stats"), &PlantGenerator::get_stats);
    ClassDB::bind_method(D_METHOD("get_last_generate_msec"), &PlantGenerator::get_last_generate_msec);

    BIND_ENUM_CONSTANT(TAG_SOLID);
    BIND_ENUM_CONSTANT(TAG_GROUND);
    BIND_ENUM_CONSTANT(TAG_NO_GROW);
}

}  // namespace godot
