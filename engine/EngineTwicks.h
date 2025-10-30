#pragma once

#define ENGINE_TWICKS_RUN_AND_LOG(FUNC)           \
    do {                                          \
        if (!(FUNC())) {                          \
            spdlog::warn("{} failed", #FUNC);     \
        } else {                                  \
            spdlog::info("{} succeeded", #FUNC);  \
        }                                         \
    } while (0)

namespace EngineTwicks
{
    bool DisableTAA();
    bool DisableCA();

    inline void DisableBadEffects() {
        spdlog::info("Disabling bad effects");
        ENGINE_TWICKS_RUN_AND_LOG(DisableTAA);
        ENGINE_TWICKS_RUN_AND_LOG(DisableCA);
    }
};

