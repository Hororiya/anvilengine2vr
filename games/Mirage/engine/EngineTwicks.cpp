#include "EngineTwicks.h"
#include <engine/memory/offsets.h>

namespace EngineTwicks
{
    bool DisableTAA() {
        auto                 addr  = memory::disable_taa_addr();
        std::vector<uint8_t> patch = { 0x41, 0xC6, 0x80, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x87, 0xD0, 0x00, 0x00, 0x00 };
        return memory::PatchMemory(addr, patch);
    }

    bool DisableCA() {
        auto                 addr  = memory::disable_ca_addr();
        std::vector<uint8_t> patch =  { 0x80, 0x79, 0x32, 0x01, 0x75, 0x4E, 0x49, 0x8B, 0xCE };
        return memory::PatchMemory(addr, patch);
    }

}; // namespace EngineTwicks
