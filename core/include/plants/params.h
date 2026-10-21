// Every tunable number of the generator, in one table.
// The table is exposed through the C API (name, default, min, max), so the web demo and a future
// Godot inspector can build their UI from it instead of duplicating the list.
#pragma once

namespace plants {

//  X(Id, "name", default, min, max)
#define PLANTS_PARAMS(X)                                          \
    X(ClimberSeeds, "climber.seeds", 28, 0, 64)                   \
    X(ClimberRoofSeeds, "climber.roofSeeds", 8, 0, 32)            \
    X(ClimberBudget, "climber.budget", 15000, 200, 40000)          \
    X(ClimberStep, "climber.step", 0.09f, 0.04f, 0.2f)            \
    X(ClimberMaxSteps, "climber.maxSteps", 170, 10, 600)          \
    X(ClimberBranchChance, "climber.branchChance", 0.12f, 0, 0.3f) \
    X(ClimberUp, "climber.up", 0.42f, 0, 2)                       \
    X(ClimberRandom, "climber.random", 0.75f, 0, 2)               \
    X(ClimberAdhesion, "climber.adhesion", 1.0f, 0, 3)            \
    X(ClimberAdhesionDist, "climber.adhesionDist", 0.45f, 0.1f, 1.5f) \
    X(ClimberMaxFloat, "climber.maxFloat", 0.3f, 0.05f, 2)        \
    X(ClimberHangChance, "climber.hangChance", 0.6f, 0, 1)        \
    X(ClimberHangLength, "climber.hangLength", 2.4f, 0.2f, 8)     \
    X(ClimberLeafSize, "climber.leafSize", 0.17f, 0.04f, 0.4f)    \
    X(ClimberLeafDensity, "climber.leafDensity", 1.25f, 0.2f, 3)   \
    X(ClimberFlowerChance, "climber.flowerChance", 0.025f, 0, 0.3f) \
    X(ClimberStemRadius, "climber.stemRadius", 0.016f, 0.003f, 0.05f) \
    X(BushCount, "bush.count", 16, 0, 80)                         \
    X(BushRoofShare, "bush.roofShare", 0.35f, 0, 1)               \
    X(BushRadius, "bush.radius", 0.75f, 0.2f, 2.5f)               \
    X(BushHeight, "bush.height", 0.85f, 0.3f, 2)                  \
    X(BushClumps, "bush.clumps", 9, 2, 24)                        \
    X(BushLeafSize, "bush.leafSize", 0.095f, 0.03f, 0.3f)         \
    X(BushLeafDensity, "bush.leafDensity", 230, 40, 800)          \
    X(BushFlowering, "bush.flowering", 0.35f, 0, 1)               \
    X(GrassBlades, "grass.blades", 18, 3, 40)                     \
    X(GrassHeight, "grass.height", 0.26f, 0.05f, 1.2f)            \
    X(GrassFlowerChance, "grass.flowerChance", 0.3f, 0, 1)        \
    X(ShadeBlend, "shade.normalBlend", 0.78f, 0, 1)               \
    X(Lod1Keep, "lod1.leafKeep", 0.3f, 0.05f, 1)                 \
    X(Lod2Keep, "lod2.leafKeep", 0.06f, 0, 0.5f)                  \
    X(IvyClumpCell, "lod.ivyClumpCell", 0.6f, 0.2f, 2)            \
    X(Lod3MergeCell, "lod3.mergeCell", 2.6f, 0.4f, 5)

enum ParamId {
#define PLANTS_PARAM_ENUM(id, name, def, lo, hi) kParam##id,
    PLANTS_PARAMS(PLANTS_PARAM_ENUM)
#undef PLANTS_PARAM_ENUM
    kParamCount
};

struct ParamInfo {
    const char* name;
    float defaultValue, minValue, maxValue;
};

const ParamInfo& paramInfo(int id);

struct Params {
    float v[kParamCount];
    Params() { reset(); }
    void reset() {
        for (int i = 0; i < kParamCount; ++i) v[i] = paramInfo(i).defaultValue;
    }
    float operator[](ParamId id) const { return v[id]; }
    int i(ParamId id) const { return static_cast<int>(v[id] + 0.5f); }
};

}  // namespace plants
