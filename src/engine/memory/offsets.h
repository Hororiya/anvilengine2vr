#pragma once
#include "memory/memory_mul.h"

namespace memory
{

    inline uintptr_t disable_taa_addr()
    {
        static auto pattern = "F3 41 0F 11 42 08 F3 0F 10 ? ? ? ? ? F3 41 0F 11 4A 0C";
        static auto addr    = FuncRelocation("disable_taa_addr", pattern, 0x52ece6);
        return addr;
    }

    inline uintptr_t begin_engine_frame_fn_addr()
    {
        return (uintptr_t) g_mod + 0x10f46f0;
    }


}
