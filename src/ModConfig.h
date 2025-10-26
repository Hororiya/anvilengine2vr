#pragma once

namespace ModSettings
{
    struct ACOdysseyUISettings {
        float hudScaleX{0.4};
        float hudScaleY{0.4 * 1024.0f/1920.0f};
    };

    extern ACOdysseyUISettings g_ac_odyssey_ui_settings;
} // namespace ModSettings
