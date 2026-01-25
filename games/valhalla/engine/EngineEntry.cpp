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


void EngineEntry::on_pre_imgui_frame()
{
    static bool menu_button_was_pressed = false;
    static std::chrono::steady_clock::time_point menu_button_press_start{};
    const bool start_button_pressed = ImGui::IsKeyDown(ImGuiKey_GamepadStart);
    const bool menu_button_pressed = ImGui::IsKeyDown(ImGuiKey_GamepadBack);

    static bool ui_toggle_active = false;
    const bool both_clicks_down = start_button_pressed && menu_button_pressed;

    if (both_clicks_down && !ui_toggle_active) {
        ui_toggle_active = true;
        ModSettings::g_internalSettings.forceFlatScreen = !ModSettings::g_internalSettings.forceFlatScreen;
        return;
    }
    if (!menu_button_pressed || !start_button_pressed) {
        ui_toggle_active = false;
    }


    constexpr auto long_press_duration = std::chrono::seconds(1);

    if (start_button_pressed) {
        if (!menu_button_was_pressed) {
            menu_button_press_start = std::chrono::steady_clock::now();
            menu_button_was_pressed = true;
        } else {
            auto elapsed = std::chrono::steady_clock::now() - menu_button_press_start;
            if (elapsed >= long_press_duration) {
                g_framework->set_draw_ui(!g_framework->is_drawing_ui());
                menu_button_press_start = std::chrono::steady_clock::now();
            }
        }
    } else {
        menu_button_was_pressed = false;
    }
}


void EngineEntry::on_draw_ui()
{
    if (!ImGui::CollapsingHeader(get_name().data())) {
        return;
    }

    if(m_decoupled_pitch->draw("Decoupled Camera Pitch")) {
        update(true);
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
    ModSettings::g_ac_valhalla_settings.hudScaleX = m_hud_scaleX->value();
    ModSettings::g_ac_valhalla_settings.hudScaleY = m_hud_scaleY->value();
    ModSettings::g_ac_valhalla_settings.decoupledPitch = m_decoupled_pitch->value();
}
