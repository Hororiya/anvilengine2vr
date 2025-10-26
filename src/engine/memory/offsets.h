#pragma once
#include "memory/memory_mul.h"

namespace memory
{

    inline uintptr_t disable_taa_addr()
    {
        static auto pattern = "C7 42 38 33 33 33 3F C6 82 A0 00 00 00 01";
        static auto addr    = FuncRelocation("disable_taa_addr", pattern, 0x813919);
        return addr;
    }

    inline uintptr_t disable_taa2_addr()
    {
        static auto pattern = "85 DB 74 41 83 EB 01";
        static auto addr    = FuncRelocation("disable_taa2_addr", pattern, 0x8138cd);
        return addr;
    }

    inline uintptr_t calc_projection_fn_addr() {
        return (uintptr_t) g_mod + 0xbd325a0;
    }

    inline uintptr_t on_calc_mvp_fn_addr() {
        return (uintptr_t) g_mod + 0xbdff5b0;
    }

    inline uintptr_t on_begin_frame_fn_addr() {
        // unconfirmed
        return (uintptr_t) g_mod + 0x1a037e0;
    }

    inline uintptr_t on_task_fn_addr() {
//        return (uintptr_t) g_mod + 0xad44120;
        return (uintptr_t) g_mod + 0xa4a250;
    }

    inline uintptr_t on_frame_fn_addr() {
        // unconfirmed
//        return (uintptr_t) g_mod + 0xa4a250;
        return (uintptr_t) g_mod + 0x1922810;
    }

    inline uintptr_t g_current_ui_pointer_addr() {
        return (uintptr_t) g_mod + 0x50A0D38;
    }

    inline uintptr_t g_hud_overlay_pointer() {
        return (uintptr_t) g_mod + 0x56c0cd0;
    }

    /*
     * get forward vector from camera node for aiming calculations
     */
    inline uintptr_t camera_node_get_forward_addr() {
        return (uintptr_t) g_mod + 0xf644470;
    }

    inline uintptr_t calc_ui_viewport_addr() {
        return (uintptr_t) g_mod + 0xbcdf9f0;
    }

    inline uintptr_t g_ui_viewport_addr() {
        return (uintptr_t) g_mod + 0x49e9898;
    }
}
