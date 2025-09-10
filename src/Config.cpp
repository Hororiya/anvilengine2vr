#include "Mods.hpp"


Mods::Mods() {
    m_mods.emplace_back(GOWVRConfig::get());
    m_mods.emplace_back(VR::get());
    m_mods.emplace_back(EngineEntry::Get());
}
