#include "plant_layer.h"

#include <chrono>
#include <cmath>
#include <cstring>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

namespace godot {

namespace {

constexpr int kStride = 16;           // floats per instance: 3x4 transform, then custom data
constexpr float kCellMargin = 12.0f;  // instances reach this far past their cell's edge
constexpr float kWindMargin = 0.4f;   // wind sway beyond the mesh bounds

enum Visibility { kOutside, kPartly, kInside };

// Where the box lies against the frustum. Camera3D::get_frustum() planes face out.
Visibility box_in_frustum(const Vector3& lo, const Vector3& hi, const Plane* planes, int count) {
    Visibility v = kInside;
    for (int i = 0; i < count; ++i) {
        const Vector3& n = planes[i].normal;
        const Vector3 inner(n.x > 0 ? lo.x : hi.x, n.y > 0 ? lo.y : hi.y, n.z > 0 ? lo.z : hi.z);
        if (n.dot(inner) - planes[i].d > 0) return kOutside;
        const Vector3 outer(n.x > 0 ? hi.x : lo.x, n.y > 0 ? hi.y : lo.y, n.z > 0 ? hi.z : lo.z);
        if (n.dot(outer) - planes[i].d > 0) v = kPartly;
    }
    return v;
}

}  // namespace

PlantLayer::~PlantLayer() { clear_variants(); }

void PlantLayer::configure(const Ref<DemoWorld>& world, int kind, float cell_size, float max_distance, float y_min,
                           float y_max, float fade) {
    world_ = world;
    kind_ = kind;
    cell_size_ = cell_size;
    max_distance_ = max_distance;
    y_min_ = y_min;
    y_max_ = y_max;
    fade_ = fade;
    blocks_.clear();
}

void PlantLayer::set_variant(int index, const Array& parts) {
    ERR_FAIL_COND(index < 0);
    if (index >= int(variants_.size())) variants_.resize(index + 1);
    PlantVariant& v = variants_[index];
    free_variant(v);
    RenderingServer* rs = RenderingServer::get_singleton();
    for (int64_t pi = 0; pi < parts.size(); ++pi) {
        const Dictionary d = parts[pi];
        const Array lods = d.get("lods", Array());
        const PackedFloat32Array ranges = d.get("ranges", PackedFloat32Array());
        const Ref<Material> material = d.get("material", Variant());
        // shadow_lods[i]: the LOD whose mesh casts LOD i's shadow, -1 for none. Missing entries cast nothing.
        const PackedInt32Array shadow_lods = d.get("shadow_lods", PackedInt32Array());
        ERR_CONTINUE_MSG(ranges.size() == 0 || ranges.size() > lods.size(), "Each part needs a range per drawn LOD.");
        Part part;
        part.ranges.assign(ranges.ptr(), ranges.ptr() + ranges.size());
        part.buckets.resize(ranges.size());
        part.far.resize(ranges.size());
        part.proxy.resize(ranges.size());
        part.caster.assign(ranges.size(), -1);
        part.tile_proto.resize(ranges.size());
        part.material = material;
        // Fills in a bucket for a LOD drawn with the mesh of mesh_lod; without shadows set, it is only a template.
        auto prepare = [&](Bucket& b, int lod, int mesh_lod) {
            b.lod = lod;
            b.mesh = lods[mesh_lod];
            if (b.mesh.is_null()) return false;
            const AABB box = b.mesh->get_aabb();
            b.local_center = box.get_center();
            b.local_radius = box.size.length() * 0.5f + kWindMargin;
            Ref<ArrayMesh> am = b.mesh;
            if (am.is_valid())
                for (int s = 0; s < am->get_surface_count(); ++s) b.triangles += am->surface_get_array_index_len(s) / 3;
            b.live = true;
            return true;
        };
        using SC = RenderingServer::ShadowCastingSetting;
        auto make = [&](Bucket& b, int lod, int mesh_lod, SC shadows) {
            if (prepare(b, lod, mesh_lod)) create(b, material, shadows);
        };
        for (int lod = 0; lod < int(ranges.size()); ++lod) {
            int caster = lod < shadow_lods.size() ? shadow_lods[lod] : -1;
            if (caster >= lods.size() || (caster >= 0 && Ref<Mesh>(lods[caster]).is_null())) caster = -1;
            part.caster[lod] = caster;
            if (shadow_tiles_) {
                make(part.buckets[lod], lod, lod, SC::SHADOW_CASTING_SETTING_OFF);
                if (caster >= 0) prepare(part.tile_proto[lod], lod, caster);
                continue;
            }
            const bool casts = caster == lod;
            make(part.buckets[lod], lod, lod, casts ? SC::SHADOW_CASTING_SETTING_ON : SC::SHADOW_CASTING_SETTING_OFF);
            if (casts) make(part.far[lod], lod, lod, SC::SHADOW_CASTING_SETTING_OFF);
            if (caster >= 0 && !casts) make(part.proxy[lod], lod, caster, SC::SHADOW_CASTING_SETTING_SHADOWS_ONLY);
        }
        v.parts.push_back(std::move(part));
    }
}

void PlantLayer::create(Bucket& b, const Ref<Material>& material, RenderingServer::ShadowCastingSetting shadows) {
    RenderingServer* rs = RenderingServer::get_singleton();
    b.multimesh = rs->multimesh_create();
    rs->multimesh_set_mesh(b.multimesh, b.mesh->get_rid());
    b.instance = rs->instance_create();
    rs->instance_set_base(b.instance, b.multimesh);
    if (scenario_.is_valid()) rs->instance_set_scenario(b.instance, scenario_);
    if (material.is_valid()) rs->instance_geometry_set_material_override(b.instance, material->get_rid());
    rs->instance_geometry_set_cast_shadows_setting(b.instance, shadows);
    rs->instance_set_visible(b.instance, false);
    b.created = true;
}

void PlantLayer::free_variant(PlantVariant& v) {
    RenderingServer* rs = RenderingServer::get_singleton();
    auto free_bucket = [rs](Bucket& b) {
        if (!b.created) return;
        rs->free_rid(b.instance);
        rs->free_rid(b.multimesh);
    };
    for (Part& p : v.parts) {
        for (std::vector<Bucket>* list : {&p.buckets, &p.far, &p.proxy})
            for (Bucket& b : *list) free_bucket(b);
        for (auto& t : p.tiles) free_bucket(t.second);
    }
    v.parts.clear();
}

PlantLayer::Bucket& PlantLayer::tile(Part& part, int lod, const overgrowth::Instance& inst, float distance) {
    const int cls = distance < 64.0f ? 0 : (distance < 160.0f ? 1 : 2);
    const float size = shadow_tile_ * float(1 << cls);
    const int64_t tx = int64_t(std::floor(inst.x / size)), tz = int64_t(std::floor(inst.z / size));
    const int64_t key = (int64_t(lod) << 56) | (int64_t(cls) << 52) | ((tx & 0x3FFFFFF) << 26) | (tz & 0x3FFFFFF);
    auto it = part.tiles.find(key);
    if (it != part.tiles.end()) return it->second;
    return part.tiles.emplace(key, part.tile_proto[lod]).first->second;
}

void PlantLayer::clear_variants() {
    for (PlantVariant& v : variants_) free_variant(v);
    variants_.clear();
}

PlantLayer::Block& PlantLayer::block(int bx, int bz) {
    const int64_t key = (int64_t(bx) << 32) ^ int64_t(uint32_t(bz));
    auto it = blocks_.find(key);
    if (it != blocks_.end()) return *it->second;
    return *blocks_.emplace(key, std::make_unique<Block>()).first->second;
}

const PlantLayer::Cell& PlantLayer::cell(Block& b, int cx, int cz) {
    const int bit = (cz & (kBlock - 1)) * kBlock + (cx & (kBlock - 1));
    Cell& c = b.cells[bit];
    if (b.generated & (uint64_t(1) << bit)) return c;
    b.generated |= uint64_t(1) << bit;
    world_->world().generate_cell(kind_, cx, cz, cell_size_, c.instances);
    float y0 = 1e30f, y1 = -1e30f;
    for (auto& i : c.instances) {
        y0 = std::min(y0, i.y);
        y1 = std::max(y1, i.y);
        i.cos_scaled = std::cos(i.yaw) * i.scale;
        i.sin_scaled = std::sin(i.yaw) * i.scale;
    }
    c.y0 = y0 + y_min_;
    c.y1 = y1 + y_max_;
    if (!c.instances.empty()) {
        b.y0 = std::min(b.y0, c.y0);
        b.y1 = std::max(b.y1, c.y1);
    }
    return c;
}

void PlantLayer::push(Bucket& b, const overgrowth::Instance& inst, float fade_code) {
    if (!b.live) return;
    if (b.count >= b.capacity) {
        // Grow by doubling; the staging data keeps what was pushed this frame. The multimesh follows in commit().
        b.capacity = std::max(64, b.capacity * 2);
        b.staging.resize(size_t(b.capacity) * kStride);
    }
    // Rotation about Y and a uniform scale; row-major 3x4 as MultiMesh buffers expect.
    const float c = inst.cos_scaled, s = inst.sin_scaled;
    float* o = b.staging.data() + size_t(b.count) * kStride;
    o[0] = c;    o[1] = 0;           o[2] = s;   o[3] = inst.x;
    o[4] = 0;    o[5] = inst.scale;  o[6] = 0;   o[7] = inst.y;
    o[8] = -s;   o[9] = 0;           o[10] = c;  o[11] = inst.z;
    o[12] = fade_code;
    o[13] = float(b.lod);
    o[14] = 0;
    o[15] = 0;
    // Bounds of the bucket: the rotated mesh centre plus a radius that covers any rotation.
    const Vector3 lc = b.local_center;
    const Vector3 center(inst.x + c * lc.x + s * lc.z, inst.y + inst.scale * lc.y, inst.z - s * lc.x + c * lc.z);
    const float r = b.local_radius * inst.scale;
    const Vector3 lo = center - Vector3(r, r, r), hi = center + Vector3(r, r, r);
    b.lo = b.count == 0 ? lo : b.lo.min(lo);
    b.hi = b.count == 0 ? hi : b.hi.max(hi);
    ++b.count;
    ++stats_.pushes;
}

void PlantLayer::commit(Bucket& b) {
    if (!b.created) return;
    RenderingServer* rs = RenderingServer::get_singleton();
    const bool shown = b.count > 0 && is_visible_in_tree();
    if (shown != b.shown) {
        rs->instance_set_visible(b.instance, shown);
        b.shown = shown;
    }
    if (b.count == 0) return;
    if (b.allocated != b.capacity) {
        rs->multimesh_allocate_data(b.multimesh, b.capacity, RenderingServer::MULTIMESH_TRANSFORM_3D, false, true, false);
        b.data.resize(int64_t(b.capacity) * kStride);
        b.allocated = b.capacity;
    }
    // A tight box lets Godot skip this bucket for shadow cascades it cannot reach. Set before the buffer, so Godot
    // does not compute the bounds of every instance itself.
    rs->multimesh_set_custom_aabb(b.multimesh, AABB(b.lo, b.hi - b.lo));
    std::memcpy(b.data.ptrw(), b.staging.data(), size_t(b.count) * kStride * sizeof(float));
    rs->multimesh_set_buffer(b.multimesh, b.data);
    rs->multimesh_set_visible_instances(b.multimesh, b.count);
    stats_.triangles += b.triangles * b.count;
    ++stats_.draws;
}

namespace {

// The batch update_layers() hands to walk_task(). Only the main thread starts batches, one at a time.
std::vector<PlantLayer*>* g_batch = nullptr;
const void* g_view = nullptr;

}  // namespace

void PlantLayer::update(Camera3D* camera, const Vector3& sun_direction, float shadow_distance) {
    TypedArray<PlantLayer> one;
    one.push_back(this);
    update_layers(one, camera, sun_direction, shadow_distance);
}

double PlantLayer::update_layers(const TypedArray<PlantLayer>& layers, Camera3D* camera, const Vector3& sun_direction,
                                 float shadow_distance) {
    ERR_FAIL_NULL_V(camera, 0.0);
    const auto t0 = std::chrono::steady_clock::now();
    View view;
    const TypedArray<Plane> frustum = camera->get_frustum();
    for (int64_t i = 0; i < frustum.size(); ++i) view.planes.push_back(frustum[i]);
    view.cam = camera->get_global_position();
    view.away_from_sun = -sun_direction.normalized();
    view.shadow_distance = shadow_distance;
    std::vector<PlantLayer*> active;
    for (int64_t i = 0; i < layers.size(); ++i) {
        PlantLayer* l = Object::cast_to<PlantLayer>(layers[i]);
        if (l && l->begin()) active.push_back(l);
    }
    if (active.size() == 1) {
        active[0]->walk(view);
    } else if (!active.empty()) {
        g_batch = &active;
        g_view = &view;
        WorkerThreadPool* pool = WorkerThreadPool::get_singleton();
        const int64_t task = pool->add_group_task(callable_mp_static(&PlantLayer::walk_task), int32_t(active.size()), -1,
                                                  true, "PlantLayer walk");
        pool->wait_for_group_task_completion(task);
        g_batch = nullptr;
        g_view = nullptr;
    }
    for (PlantLayer* l : active) l->finish();
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
}

void PlantLayer::walk_task(int index) { (*g_batch)[index]->walk(*static_cast<const View*>(g_view)); }

bool PlantLayer::begin() {
    for_each_bucket([](Bucket& b) { b.count = 0; });
    stats_ = Stats{};
    if (world_.is_null() || !is_visible_in_tree()) {
        apply_visibility();
        return false;
    }
    return true;
}

void PlantLayer::walk(const View& view) {
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<Plane>& planes = view.planes;
    const Vector3 cam = view.cam;
    const float shadow_distance = view.shadow_distance;
    const float s = cell_size_;
    const int r = int(std::ceil(max_distance_ / s));
    const int ccx = int(std::floor(cam.x / s)), ccz = int(std::floor(cam.z / s));
    const float max_d2 = max_distance_ * max_distance_;
    const float shadow_d2 = shadow_distance * shadow_distance;
    // Casters outside the view still shade it: a cell counts if its box, swept away from the sun down to the
    // ground, reaches into the frustum.
    const Vector3 sweep =
        view.away_from_sun * std::min(160.0f, (y_max_ - y_min_ + 10.0f) / std::max(-view.away_from_sun.y, 0.25f));

    // The world's terrain stays within these heights; a block whose cells are not all generated yet is tested
    // with them.
    constexpr float kGroundLo = -60.0f, kGroundHi = 80.0f;
    auto in_view = [&](const Vector3& lo, const Vector3& hi, float d2) {
        const Visibility v = box_in_frustum(lo, hi, planes.data(), int(planes.size()));
        if (v != kOutside || d2 >= shadow_d2) return v;
        return box_in_frustum(lo.min(lo + sweep), hi.max(hi + sweep), planes.data(), int(planes.size())) == kOutside
                   ? kOutside
                   : kPartly;
    };
    auto box_dist2 = [&](float x0, float z0, float size) {
        const float dx = std::max({x0 - cam.x, 0.0f, cam.x - (x0 + size)});
        const float dz = std::max({z0 - cam.z, 0.0f, cam.z - (z0 + size)});
        return dx * dx + dz * dz;
    };
    const float bs = s * kBlock;
    const auto floor_div = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    for (int bz = floor_div(ccz - r, kBlock); bz <= floor_div(ccz + r, kBlock); ++bz) {
        for (int bx = floor_div(ccx - r, kBlock); bx <= floor_div(ccx + r, kBlock); ++bx) {
            const float bd2 = box_dist2(bx * bs, bz * bs, bs);
            if (bd2 > max_d2) continue;
            Block& blk = block(bx, bz);
            const bool complete = blk.generated == ~uint64_t(0);
            if (complete && blk.y0 > blk.y1) continue;  // no instances at all
            const float y0 = complete ? blk.y0 : kGroundLo + y_min_, y1 = complete ? blk.y1 : kGroundHi + y_max_;
            const Visibility bv = in_view(Vector3(bx * bs - kCellMargin, y0, bz * bs - kCellMargin),
                                          Vector3((bx + 1) * bs + kCellMargin, y1, (bz + 1) * bs + kCellMargin), bd2);
            if (bv == kOutside) continue;
            for (int cz = bz * kBlock; cz < (bz + 1) * kBlock; ++cz) {
                for (int cx = bx * kBlock; cx < (bx + 1) * kBlock; ++cx) {
                    const float x0 = cx * s, z0 = cz * s;
                    const float d2 = box_dist2(x0, z0, s);
                    if (d2 > max_d2) continue;
                    const Cell& c = cell(blk, cx, cz);
                    if (c.instances.empty()) continue;
                    // Cells of a block that is wholly in view need no test of their own.
                    if (bv == kPartly &&
                        in_view(Vector3(x0 - kCellMargin, c.y0, z0 - kCellMargin), Vector3(x0 + s + kCellMargin, c.y1, z0 + s + kCellMargin), d2) == kOutside)
                        continue;
                    ++stats_.cells;
                    for (const auto& inst : c.instances) {
                        const float d = cam.distance_to(Vector3(inst.x, inst.y, inst.z));
                        if (d > max_distance_ || inst.variant < 0 || inst.variant >= int(variants_.size())) continue;
                        ++stats_.instances;
                        for (Part& part : variants_[inst.variant].parts) {
                            const std::vector<float>& ranges = part.ranges;
                            int lod = 0;
                            while (lod < int(ranges.size()) && d >= ranges[lod]) ++lod;
                            if (lod >= int(ranges.size())) continue;
                            ++stats_.per_lod[std::min(lod, 4)];
                            const float band = ranges[lod] * fade_;
                            const float t = (d - (ranges[lod] - band)) / band;
                            const bool near = d <= shadow_distance;
                            auto put = [&](int l, float code) {
                                if (shadow_tiles_) {
                                    push(part.buckets[l], inst, code);
                                    if (near && part.caster[l] >= 0) push(tile(part, l, inst, d), inst, code);
                                    return;
                                }
                                push((!near && part.far[l].live) ? part.far[l] : part.buckets[l], inst, code);
                                if (near) push(part.proxy[l], inst, code);
                            };
                            if (t <= 0) {
                                put(lod, 1.0f);
                            } else {
                                put(lod, 1.0f + t);
                                if (lod + 1 < int(ranges.size())) put(lod + 1, t);
                            }
                        }
                    }
                }
            }
        }
    }
    stats_.walk_usec = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
}

void PlantLayer::finish() {
    const auto t0 = std::chrono::steady_clock::now();
    // Shadow tiles get their multimesh on first use, and give it back after some seconds unused.
    RenderingServer* rs = RenderingServer::get_singleton();
    for (PlantVariant& v : variants_) {
        for (Part& p : v.parts) {
            for (auto it = p.tiles.begin(); it != p.tiles.end();) {
                Bucket& b = it->second;
                if (b.count > 0) {
                    b.idle_frames = 0;
                    if (!b.created) create(b, p.material, RenderingServer::SHADOW_CASTING_SETTING_SHADOWS_ONLY);
                } else if (++b.idle_frames > 240) {
                    if (b.created) {
                        rs->free_rid(b.instance);
                        rs->free_rid(b.multimesh);
                    }
                    it = p.tiles.erase(it);
                    continue;
                }
                ++it;
            }
        }
    }
    for_each_bucket([this](Bucket& b) { commit(b); });
    stats_.upload_usec = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    stats_.usec = stats_.walk_usec + stats_.upload_usec;
}

void PlantLayer::apply_visibility() {
    RenderingServer* rs = RenderingServer::get_singleton();
    const bool shown = is_visible_in_tree();
    for_each_bucket([&](Bucket& b) {
        if (b.created) {
            b.shown = shown && b.count > 0;
            rs->instance_set_visible(b.instance, b.shown);
        }
    });
}

Dictionary PlantLayer::get_stats() const {
    Dictionary d;
    d["instances"] = stats_.instances;
    PackedInt32Array per_lod;
    for (int n : stats_.per_lod) per_lod.push_back(n);
    d["per_lod"] = per_lod;
    d["triangles"] = stats_.triangles;
    d["draws"] = stats_.draws;
    d["usec"] = stats_.usec;
    d["upload_usec"] = stats_.upload_usec;
    d["walk_usec"] = stats_.walk_usec;
    d["cells"] = stats_.cells;
    d["pushes"] = stats_.pushes;
    d["blocks"] = int64_t(blocks_.size());
    return d;
}

void PlantLayer::_notification(int what) {
    RenderingServer* rs = RenderingServer::get_singleton();
    switch (what) {
        case NOTIFICATION_ENTER_WORLD: {
            scenario_ = get_world_3d()->get_scenario();
            for_each_bucket([&](Bucket& b) {
                if (b.created) rs->instance_set_scenario(b.instance, scenario_);
            });
        } break;
        case NOTIFICATION_EXIT_WORLD: {
            scenario_ = RID();
            for_each_bucket([&](Bucket& b) {
                if (b.created) rs->instance_set_scenario(b.instance, RID());
            });
        } break;
        case NOTIFICATION_VISIBILITY_CHANGED: apply_visibility(); break;
        default: break;
    }
}

void PlantLayer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "world", "kind", "cell_size", "max_distance", "y_min", "y_max", "fade"),
                         &PlantLayer::configure, DEFVAL(0.12f));
    ClassDB::bind_method(D_METHOD("set_shadow_tiles", "tile"), &PlantLayer::set_shadow_tiles);
    ClassDB::bind_method(D_METHOD("set_variant", "index", "parts"), &PlantLayer::set_variant);
    ClassDB::bind_method(D_METHOD("clear_variants"), &PlantLayer::clear_variants);
    ClassDB::bind_method(D_METHOD("update", "camera", "sun_direction", "shadow_distance"), &PlantLayer::update);
    ClassDB::bind_static_method("PlantLayer", D_METHOD("update_layers", "layers", "camera", "sun_direction", "shadow_distance"),
                                &PlantLayer::update_layers);
    ClassDB::bind_method(D_METHOD("get_stats"), &PlantLayer::get_stats);
    ClassDB::bind_integer_constant(get_class_static(), "", "KIND_HOUSES", overgrowth::kCellHouses);
    ClassDB::bind_integer_constant(get_class_static(), "", "KIND_BUSHES", overgrowth::kCellBushes);
    ClassDB::bind_integer_constant(get_class_static(), "", "KIND_TREES", overgrowth::kCellTrees);
    ClassDB::bind_integer_constant(get_class_static(), "", "KIND_GRASS", overgrowth::kCellGrass);
}

}  // namespace godot
