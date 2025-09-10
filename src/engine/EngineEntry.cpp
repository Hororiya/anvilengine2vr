#include "EngineEntry.h"
#include "DLSSModule.h"
#include "EngineCameraModule.h"
#include "EngineRendererModule.h"
#include "EngineTwicks.h"
#include <engine/models/ModSettings.h>
#include <mods/VR.hpp>

std::optional<std::string> EngineEntry::on_initialize()
{
    DLSSModule::Get()->InstallHooks();
    EngineTwicks::DisableBadEffects();
    EngineRendererModule::Get()->InstallHooks();
    EngineCameraModule::Get()->InstallHooks();
//    UpscalerAfrNvidiaModule::Get();
    return Mod::on_initialize();
}

void EngineEntry::on_draw_ui()
{
    if (!ImGui::CollapsingHeader(get_name().data())) {
        return;
    }

    if(m_hud_scale->draw("HUD Scale"))
    {
        update(true);
    }

    if(m_hud_offset_y->draw("HUD Offset Y"))
    {
        update(true);
    }

    if(m_dlss_fix->draw("DLSS and Motion vector Fix")) {
        update(true);
    }

    if(m_tone_map_algorithm->draw("Tone Map Algorithm"))
    {
        update(true);
    }

    if(m_tone_map_exposure->draw("Tone Map Exposure"))
    {
        update(true);
    }

#ifdef _DEBUG
    if(ImGui::Checkbox("Debug Motion Vector", &ModSettings::g_internalSettings.debugShaders))
    {
        spdlog::info("Debug Motion Vector: {}", ModSettings::g_internalSettings.debugShaders);
    }
    if(ImGui::Checkbox("Camera Shake", &ModSettings::g_internalSettings.cameraShake))
    {
        spdlog::info("Camera Shake: {}", ModSettings::g_internalSettings.cameraShake);
    }

    if (ImGui::CollapsingHeader("Debug Matrices")) {
        auto& debug = ModSettings::g_debugAndCalibration;

        // Helper lambda for displaying 4x4 matrices
        auto    displayMatrix = [](const char* name, const glm::mat4& matrix) {
            if (ImGui::CollapsingHeader(name)) {
                ImGui::Columns(4, name, true);
                for (int row = 0; row < 4; row++) {
                    for (int col = 0; col < 4; col++) {
                        ImGui::Text("%.3f", matrix[col][row]);
                        if (col < 3) ImGui::NextColumn();
                    }
                    if (row < 3) ImGui::NextColumn();
                }
                ImGui::Columns(1);
            }
        };

        static auto vr = VR::get();

//        displayMatrix("Camera To Clip Matrix", debug.cameraToClip);
//        displayMatrix("Camera View Matrix", debug.cameraView);
//        displayMatrix("View Matrix", ModSettings::getView(vr->m_presenter_frame_count));
//        displayMatrix("Projection Matrix", ModSettings::getProjection(vr->m_presenter_frame_count));
    }

#endif

    // Add matrix visualization

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

void EngineEntry::update(bool force) {
    ModSettings::g_UISettings.hudScale = m_hud_scale->value();
    ModSettings::g_UISettings.hudVerticalOffset = m_hud_offset_y->value();
    ModSettings::g_internalSettings.nvidiaMotionVectorFix = m_dlss_fix->value();
    ModSettings::g_internalSettings.toneMapAlg = m_tone_map_algorithm->value();
    ModSettings::g_internalSettings.toneMapExposure = m_tone_map_exposure->value();
}
