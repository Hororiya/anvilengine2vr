#pragma once

namespace ModSettings
{
    struct ACMirageSettings
    {
        float hudScaleX{0.4};
        float hudScaleY{0.4 * 1024.0f/1920.0f};
        bool  decoupledPitch{false};
    };

    extern ACMirageSettings g_ac_mirage_settings;
} // namespace ModSettings
