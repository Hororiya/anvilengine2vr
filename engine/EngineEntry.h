#pragma once
#include <Mod.hpp>

class EngineEntry : public Mod
{
public:
    inline static std::shared_ptr<EngineEntry>& Get()
    {
        static std::shared_ptr<EngineEntry> instance{ std::make_shared<EngineEntry>() };
        return instance;
    }

    void update(bool force = false);

    [[nodiscard]] inline std::string_view get_name() const override { return "ACValhalla"; }

    std::optional<std::string> on_initialize() override;
    void                       on_draw_ui() override;
    void                       on_config_load(const utility::Config& cfg, bool set_defaults) override;
    void                       on_config_save(utility::Config& cfg) override;

private:
    inline static const std::vector<std::string> s_dominant_eye {
        "Right",
        "Left",
    };

    inline static const std::vector<std::string> s_tone_map_algorithms {
        "None",
        "Saturate",
        "Reinhard",
        "ACESFilmic",
    };

    const ModSlider::Ptr m_hud_scaleX{ ModSlider::create(generate_name("HudScaleX"), 0.1, 1.0, 0.4) };
    const ModSlider::Ptr m_hud_scaleY{ ModSlider::create(generate_name("HudScaleY"), 0.1, 1.0, (0.4f * 1024.0f / 1920.0f)) };
//    const ModSlider::Ptr m_tone_map_exposure{ ModSlider::create(generate_name("ToneMapExposure"), -2.0, 2.0, 0.0) };
//    const ModCombo::Ptr m_tone_map_algorithm{ ModCombo::create(generate_name("ToneMapAlgorithm"), s_tone_map_algorithms, 0) };
//    const ModToggle::Ptr m_dlss_fix{ModToggle::create("NvidiaDLSSFix", true)};
//    const ModSlider::Ptr m_display_distance{ ModSlider::create(generate_name("DisplayDistance"), 0.0, 3.0, 2.0) };
//    const ModToggle::Ptr m_force_flat_screen{ ModToggle::create(generate_name("Force Flat Screen"), false) };
    ValueList m_options{*m_hud_scaleX, *m_hud_scaleY};
};
