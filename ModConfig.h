#pragma once

namespace ModSettings
{
    struct ACValhallaSettings
    {
        float hudScaleX{0.4};
        float hudScaleY{0.4 * 1024.0f/1920.0f};
        bool  decoupledPitch{false};
    };

    extern ACValhallaSettings g_ac_valhalla_settings;
} // namespace ModSettings
