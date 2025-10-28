#pragma once

namespace ModSettings
{
    struct ACValhallaUISettings {
        float hudScaleX{0.4};
        float hudScaleY{0.4 * 1024.0f/1920.0f};
    };

    extern ACValhallaUISettings g_ac_valhalla_ui_settings;
} // namespace ModSettings
