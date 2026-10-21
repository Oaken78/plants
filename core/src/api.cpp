#include "plants/api.h"

#include "plants/generator.h"
#include "plants/mesh.h"

using namespace plants;

struct PlantsContext {
    Params params;
    TriangleBvh bvh;
    PlantSet set;
    MeshBuffers lods[kLodCount];
    float bounds[kLodCount][6] = {};
};

static void buildAllLods(PlantsContext* ctx) {
    finalizeShading(ctx->params, ctx->set);
    for (int lod = 0; lod < kLodCount; ++lod) {
        buildLod(ctx->set, ctx->params, lod, ctx->lods[lod]);
        const Aabb& b = ctx->lods[lod].bounds;
        float* o = ctx->bounds[lod];
        if (b.valid()) {
            o[0] = b.lo.x; o[1] = b.lo.y; o[2] = b.lo.z;
            o[3] = b.hi.x; o[4] = b.hi.y; o[5] = b.hi.z;
        } else {
            for (int i = 0; i < 6; ++i) o[i] = 0;
        }
    }
}

static bool validLod(int lod) { return lod >= 0 && lod < kLodCount; }

extern "C" {

PlantsContext* plants_create(void) { return new PlantsContext(); }
void plants_destroy(PlantsContext* ctx) { delete ctx; }

int plants_param_count(void) { return kParamCount; }
const char* plants_param_name(int id) { return paramInfo(id).name; }
float plants_param_default(int id) { return paramInfo(id).defaultValue; }
float plants_param_min(int id) { return paramInfo(id).minValue; }
float plants_param_max(int id) { return paramInfo(id).maxValue; }

void plants_set_param(PlantsContext* ctx, int id, float value) {
    if (id < 0 || id >= kParamCount) return;
    const ParamInfo& info = paramInfo(id);
    ctx->params.v[id] = clampf(value, info.minValue, info.maxValue);
}
float plants_get_param(PlantsContext* ctx, int id) { return (id >= 0 && id < kParamCount) ? ctx->params.v[id] : 0.0f; }
void plants_reset_params(PlantsContext* ctx) { ctx->params.reset(); }

void plants_set_host(PlantsContext* ctx, const float* positions, int vertexCount, const uint32_t* indices, int indexCount,
                     const uint8_t* tags) {
    std::vector<Vec3> pos(static_cast<size_t>(vertexCount));
    for (int i = 0; i < vertexCount; ++i) pos[i] = {positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]};
    std::vector<uint32_t> idx(indices, indices + indexCount);
    std::vector<uint8_t> tagList;
    if (tags) tagList.assign(tags, tags + indexCount / 3);
    ctx->bvh.build(pos, idx, tagList);
}

int plants_grow_on_host(PlantsContext* ctx, uint32_t seed) {
    ctx->set = PlantSet{};
    Rng rng(seed);
    Rng seedRng = rng.fork(1), climbRng = rng.fork(2), bushRng = rng.fork(3);
    std::vector<ClimberSeed> seeds = pickClimberSeeds(ctx->bvh, ctx->params, seedRng);
    growClimbers(ctx->bvh, ctx->params, climbRng, seeds, ctx->set);
    scatterBushesOnHost(ctx->bvh, ctx->params, bushRng, ctx->set);
    buildAllLods(ctx);
    return int(ctx->set.leaves.size());
}

int plants_make_bush(PlantsContext* ctx, uint32_t seed, int style, float scale) {
    ctx->set = PlantSet{};
    Rng rng(seed);
    growBush(ctx->params, rng, Vec3{0, 0, 0}, Vec3{0, 1, 0}, scale, style % kBushStyleCount, ctx->set);
    buildAllLods(ctx);
    return int(ctx->set.leaves.size());
}

int plants_make_grass(PlantsContext* ctx, uint32_t seed) {
    ctx->set = PlantSet{};
    Rng rng(seed);
    growGrassTuft(ctx->params, rng, ctx->set);
    buildAllLods(ctx);
    return int(ctx->set.blades.size());
}

int plants_lod_count(void) { return kLodCount; }
int plants_mesh_vertex_count(PlantsContext* ctx, int lod) { return validLod(lod) ? ctx->lods[lod].vertexCount() : 0; }
int plants_mesh_index_count(PlantsContext* ctx, int lod) { return validLod(lod) ? int(ctx->lods[lod].indices.size()) : 0; }
const float* plants_mesh_positions(PlantsContext* ctx, int lod) { return validLod(lod) ? ctx->lods[lod].positions.data() : nullptr; }
const int8_t* plants_mesh_normals(PlantsContext* ctx, int lod) { return validLod(lod) ? ctx->lods[lod].normals.data() : nullptr; }
const uint8_t* plants_mesh_shade(PlantsContext* ctx, int lod) { return validLod(lod) ? ctx->lods[lod].shade.data() : nullptr; }
const uint8_t* plants_mesh_aux(PlantsContext* ctx, int lod) { return validLod(lod) ? ctx->lods[lod].aux.data() : nullptr; }
const uint32_t* plants_mesh_indices(PlantsContext* ctx, int lod) { return validLod(lod) ? ctx->lods[lod].indices.data() : nullptr; }
const float* plants_mesh_bounds(PlantsContext* ctx, int lod) { return validLod(lod) ? ctx->bounds[lod] : nullptr; }

int plants_stat(PlantsContext* ctx, int stat) {
    const PlantSet& s = ctx->set;
    switch (stat) {
        case PLANTS_STAT_NODES: return int(s.nodes.size());
        case PLANTS_STAT_STEMS: return int(s.stems.size());
        case PLANTS_STAT_LEAVES: return int(s.leaves.size());
        case PLANTS_STAT_FLOWERS: return int(s.flowers.size());
        case PLANTS_STAT_CLUMPS: return int(s.clumps.size());
        case PLANTS_STAT_BLADES: return int(s.blades.size());
        case PLANTS_STAT_CLIMBERS: return s.climberCount;
        case PLANTS_STAT_BUSHES: return s.bushCount;
        default: return 0;
    }
}

}  // extern "C"
