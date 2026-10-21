/* Plain C API of the plant generator.
 *
 * This is the only surface the web demo uses, and the one a Godot GDExtension wrapper will call:
 *   1. plants_create()
 *   2. plants_set_host() with the model's triangles and per-triangle SurfaceTag
 *   3. optional plants_set_param() calls (names/ranges come from plants_param_*)
 *   4. plants_grow_on_host() / plants_make_bush() / plants_make_grass()
 *   5. read plants_mesh_*() for each LOD (valid until the next generate call)
 * See mesh.h for the vertex layout.
 */
#pragma once

#include <stdint.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define PLANTS_API EMSCRIPTEN_KEEPALIVE
#elif defined(_WIN32)
#define PLANTS_API __declspec(dllexport)
#else
#define PLANTS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PlantsContext PlantsContext;

enum PlantsStat {
    PLANTS_STAT_NODES = 0,
    PLANTS_STAT_STEMS = 1,
    PLANTS_STAT_LEAVES = 2,
    PLANTS_STAT_FLOWERS = 3,
    PLANTS_STAT_CLUMPS = 4,
    PLANTS_STAT_BLADES = 5,
    PLANTS_STAT_CLIMBERS = 6,
    PLANTS_STAT_BUSHES = 7,
    PLANTS_STAT_TREES = 8,
};

PLANTS_API PlantsContext* plants_create(void);
PLANTS_API void plants_destroy(PlantsContext* ctx);

PLANTS_API int plants_param_count(void);
PLANTS_API const char* plants_param_name(int id);
PLANTS_API float plants_param_default(int id);
PLANTS_API float plants_param_min(int id);
PLANTS_API float plants_param_max(int id);
PLANTS_API void plants_set_param(PlantsContext* ctx, int id, float value);
PLANTS_API float plants_get_param(PlantsContext* ctx, int id);
PLANTS_API void plants_reset_params(PlantsContext* ctx);

/* positions: xyz per vertex. indices: 3 per triangle. tags: one SurfaceTag per triangle (may be null). */
PLANTS_API void plants_set_host(PlantsContext* ctx, const float* positions, int vertexCount, const uint32_t* indices,
                                int indexCount, const uint8_t* tags);

/* Climbers, trees and bushes on the host. Returns the number of leaves. */
PLANTS_API int plants_grow_on_host(PlantsContext* ctx, uint32_t seed);
/* A standalone bush at the origin (style: 0 round, 1 tall, 2 spreading, 3 flowering). */
PLANTS_API int plants_make_bush(PlantsContext* ctx, uint32_t seed, int style, float scale);
/* A standalone tree at the origin; size scales tree.height (about 0.4 small, 1 medium, 1.8 large). */
PLANTS_API int plants_make_tree(PlantsContext* ctx, uint32_t seed, float size);
/* A standalone grass tuft at the origin. */
PLANTS_API int plants_make_grass(PlantsContext* ctx, uint32_t seed);

PLANTS_API int plants_lod_count(void);
PLANTS_API int plants_mesh_vertex_count(PlantsContext* ctx, int lod);
PLANTS_API int plants_mesh_index_count(PlantsContext* ctx, int lod);
PLANTS_API const float* plants_mesh_positions(PlantsContext* ctx, int lod);
PLANTS_API const int8_t* plants_mesh_normals(PlantsContext* ctx, int lod);
PLANTS_API const uint8_t* plants_mesh_shade(PlantsContext* ctx, int lod);
PLANTS_API const uint8_t* plants_mesh_aux(PlantsContext* ctx, int lod);
PLANTS_API const uint32_t* plants_mesh_indices(PlantsContext* ctx, int lod);
/* min xyz, max xyz */
PLANTS_API const float* plants_mesh_bounds(PlantsContext* ctx, int lod);

PLANTS_API int plants_stat(PlantsContext* ctx, int stat);

#ifdef __cplusplus
}
#endif
