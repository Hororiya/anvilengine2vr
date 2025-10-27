#pragma once
#include "memory/memory_mul.h"

namespace memory
{

    inline uintptr_t calc_projection_fn_addr() {
        static const auto pattern = "48 89 E0 53 48 81 EC 90 00 00 00 0F 29 70 E8 48";
        static auto addr    = FuncRelocation("calc_projection_fn_addr", pattern, 0xbd325a0);
        return addr;
    }

    inline uintptr_t on_calc_mvp_fn_addr() {
        static const auto pattern = "48 89 E0 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 C8";
        static auto addr    = FuncRelocation("on_calc_mvp_fn_addr", pattern, 0xbdff5b0);
        return  addr;
    }

//    inline uintptr_t on_begin_frame_fn_addr() {
//        return (uintptr_t) g_mod + 0x1a037e0;
//    }

    inline uintptr_t on_task_fn_addr() {
        static const auto pattern = "40 53 48 83 EC 20 48 8B D9 48 81 C1 00 01 00 00 48 83 39 00 75 07 48 83 79 08 00 74 09 48 8B 11 48 8B 41 20 FF D0 48 8D";
        static auto addr    = FuncRelocation("on_task_fn_addr", pattern, 0xa4a250);
        return addr;
    }

//    inline uintptr_t on_frame_fn_addr() {
//        return (uintptr_t) g_mod + 0x1922810;
//    }

    inline uintptr_t g_current_ui_pointer_addr() {
        static const auto pattern = "48 89 1D ? ? ? ? E8 ? ? ? ? 48 8B 7C 24 38";
        static auto addr    = InstructionRelocation("g_current_ui_pointer_addr", pattern, 3, 7, 0x50A0D38);
        return addr;
    }

    inline uintptr_t g_hud_overlay_pointer() {
        static const auto pattern = "4C 8D 0D ? ? ? ? 66 66 0F 1F 84 00 00 00 00 00 48 8D 41 B4";
        static auto addr    = InstructionRelocation("g_hud_overlay_pointer", pattern, 3, 7, 0x56C0CD0);
        return addr;
    }

    /*
     * get forward vector from camera node for aiming calculations
     */
    inline uintptr_t camera_node_get_forward_addr() {
        static const auto pattern = "0F 28 59 70 48 89";
        static auto addr    = FuncRelocation("camera_node_get_forward_addr", pattern, 0xf644470);
        return addr;
    }

    inline uintptr_t calc_ui_viewport_addr() {
        static const auto  pattern = "55 48 8D 6C 24 A9 48 81 EC E0 00 00 00 48 8B 05 ? ? ? ? 48 31 E0 48 89 45 2F";
        static auto addr    = FuncRelocation("calc_ui_viewport_addr", pattern, 0xbcdf9f0);
        return addr;
    }

    inline uintptr_t g_ui_viewport_addr() {
        static const auto pattern = "0F 11 05 ? ? ? ? E8 ? ? ? ? 48 8D 55 C7";
        static auto addr    = InstructionRelocation("g_ui_viewport_addr", pattern, 3, 7, 0x49e9898);
        return addr;
    }
}
