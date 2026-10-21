#include "plants/params.h"

namespace plants {

static const ParamInfo kParamTable[kParamCount] = {
#define PLANTS_PARAM_INFO(id, name, def, lo, hi) {name, float(def), float(lo), float(hi)},
    PLANTS_PARAMS(PLANTS_PARAM_INFO)
#undef PLANTS_PARAM_INFO
};

const ParamInfo& paramInfo(int id) {
    static const ParamInfo invalid{"", 0, 0, 0};
    return (id >= 0 && id < kParamCount) ? kParamTable[id] : invalid;
}

}  // namespace plants
