#include "Mods.hpp"
#include <engine/EngineEntry.h>
#include <mods/GOWVRConfig.hpp>
#include <mods/VR.hpp>

Mods::Mods() {
    m_mods.emplace_back(GOWVRConfig::get());
    m_mods.emplace_back(VR::get());
    m_mods.emplace_back(EngineEntry::Get());
}
