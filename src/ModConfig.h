#pragma once

namespace ModSettings
{
    struct ACOdysseySettings
    {
        float hudScaleX{0.4};
        float hudScaleY{0.4 * 1024.0f/1920.0f};
        bool  decoupledPitch{false};
    };

    extern ACOdysseySettings g_ac_odyssey_settings;
} // namespace ModSettings
