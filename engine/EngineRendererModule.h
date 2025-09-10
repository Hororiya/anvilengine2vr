#pragma once

#include <engine/memory/memory_mul.h>
#include <safetyhook/inline_hook.hpp>
#include <utility/PointerHook.hpp>
#include <utils/FunctionHook.h>

namespace sdk {
    typedef enum
    {
        SIMULATION_START               =  0,
        SIMULATION_END                 =  1,
        RENDERSUBMIT_START             =  2,
        RENDERSUBMIT_END               =  3,
        PRESENT_START                  =  4,
        PRESENT_END                    =  5,
        INPUT_SAMPLE                   =  6,
        TRIGGER_FLASH                  =  7,
        PC_LATENCY_PING                =  8,
        OUT_OF_BAND_RENDERSUBMIT_START =  9,
        OUT_OF_BAND_RENDERSUBMIT_END   = 10,
        OUT_OF_BAND_PRESENT_START      = 11,
        OUT_OF_BAND_PRESENT_END        = 12,
    } NV_LATENCY_MARKER_TYPE;

    struct NV_LATENCY_MARKER_PARAMS_V1
    {
        uint32_t  version;                                       //!< (IN) Structure version
        uint64_t  frameID;
        NV_LATENCY_MARKER_TYPE markerType;
        uint64_t  rsvd0;
        uint8_t   rsvd[56];
    };
}

class EngineRendererModule
{
public:
    void InstallHooks();

    inline static EngineRendererModule* Get()
    {
        static auto instance(new EngineRendererModule);
        return instance;
    }
private:
    EngineRendererModule()  = default;
    ~EngineRendererModule() = default;

    safetyhook::InlineHook m_checkResolutionHook{};
    safetyhook::InlineHook m_submitMarkerHook{};

    static uintptr_t submit_nvidia_marker(uintptr_t pDevice, sdk::NV_LATENCY_MARKER_PARAMS_V1* marker);
    static bool      checkResolution(int *outFlags, int maxMessagesToProcess, char filterSpecialMessages);
};
