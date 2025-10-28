#include "EngineEntry.h"
#include "EngineCameraModule.h"
#include "EngineRendererModule.h"
#include <ModConfig.h>
#include <ModSettings.h>
#include <mods/VR.hpp>

std::optional<std::string> EngineEntry::on_initialize()
{
    EngineRendererModule::Get()->InstallHooks();
    EngineCameraModule::Get()->InstallHooks();
    return Mod::on_initialize();
}

void EngineEntry::on_draw_ui()
{
    if (!ImGui::CollapsingHeader(get_name().data())) {
        return;
    }


    if (m_hud_scaleX->draw("HUD Scale X")) {
        update(true);
    }

    if (m_hud_scaleY->draw("HUD Scale Y")) {
        update(true);
    }

    //
    //    if(m_tone_map_algorithm->draw("Tone Map Algorithm"))
    //    {
    //        update(true);
    //    }
    //
    //    if(m_tone_map_exposure->draw("Tone Map Exposure"))
    //    {
    //        update(true);
    //    }
}

void EngineEntry::on_config_load(const utility::Config& cfg, bool set_defaults)
{
    for (IModValue& option : m_options) {
        option.config_load(cfg, set_defaults);
    }
    update(true);
}

void EngineEntry::on_config_save(utility::Config& cfg)
{
    for (IModValue& option : m_options) {
        option.config_save(cfg);
    }
}

void EngineEntry::update(bool force)
{
    ModSettings::g_ac_valhalla_ui_settings.hudScaleX = m_hud_scaleX->value();
    ModSettings::g_ac_valhalla_ui_settings.hudScaleY = m_hud_scaleY->value();
}
