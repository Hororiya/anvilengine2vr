#pragma once

namespace ModSettings
{
    struct ModSpecificSettings
    {
        float hudScaleX{0.4};
        float hudScaleY{0.4 * 1024.0f/1920.0f};
        bool  decoupledPitch{false};
    };

    extern ModSpecificSettings g_mod_settings;
} // namespace ModSettings
