#include "Mods.hpp"
#include "ModConfig.h"
#include <engine/EngineEntry.h>
#include <mods/VR.hpp>
#include <mods/VRConfig.hpp>

namespace ModSettings {
    ACOdysseySettings g_ac_odyssey_settings;
}

Mods::Mods() {
    m_mods.emplace_back(VRConfig::get());
    m_mods.emplace_back(VR::get());
    m_mods.emplace_back(EngineEntry::Get());
}
