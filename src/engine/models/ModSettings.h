#pragma once

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/vec3.hpp>
#include <engine/models/GOWGameState.h>

namespace ModSettings
{
    struct UISettings
    {
        float hudScale{ 1.0 };
        float hudVerticalOffset{ 0.0 };
    };

    extern UISettings g_UISettings;

    struct CrosshairTranslate
    {
        float x{ 50.0 };
        float y{ 50.0 };
    };

    static_assert(offsetof(CrosshairTranslate, y) == 4, "Crosshair::y offset");

    struct DebugAndCalibration
    {
    };

    extern DebugAndCalibration g_debugAndCalibration;

    extern CrosshairTranslate g_crosshairTranslate;
    extern GOWGameState        g_game_state;


    struct InternalSettings
    {
        bool      showQuadDisplay{ false };
        float     quadDisplayDistance{ 2.0 };
        float     worldScale{ 1.0 };
        bool      leftHandedControls{ false };
        bool      debugShaders{ false };
        bool      cameraShake{ false };
        bool      nvidiaMotionVectorFix{ false };
        int       toneMapAlg{ 0 }; // None, Saturate, Reinhard, ACESFilmic
        float     toneMapExposure{ 0.0f };
    };

    extern InternalSettings g_internalSettings;
} // namespace ModSettings
