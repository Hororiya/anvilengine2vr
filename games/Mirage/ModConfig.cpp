#include "Mods.hpp"
#include "ModConfig.h"
#include <engine/EngineEntry.h>
#include <mods/VR.hpp>
#include <mods/VRConfig.hpp>

void VR::on_xinput_get_state(uint32_t* retval, uint32_t user_index, XINPUT_STATE* state) {}

namespace ModSettings {
    ACMirageSettings g_ac_mirage_settings;
}

Mods::Mods() {
    m_mods.emplace_back(VRConfig::get());
    m_mods.emplace_back(VR::get());
    m_mods.emplace_back(EngineEntry::Get());
}
