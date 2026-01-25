#pragma once
#include "memory/memory_mul.h"

namespace memory
{
    inline uintptr_t disable_taa2_addr()
    {
        static auto pattern = "85 DB 74 41 83 EB 01";
        static auto addr    = FuncRelocation("disable_taa2_addr", pattern, 0x8138cd);
        return addr;
    }

    inline uintptr_t begin_render_frame_fn_addr()
    {
        static const auto pattern = "40 53 48 83 EC 20 48 8B D9 48 8B 0D ? ? ? ? E8 ? ? ? ? 48 8B 0D";
        static auto addr    = FuncRelocation("begin_render_frame_fn_addr", pattern, 0x10f4db0);
        return addr;
    }

    inline uintptr_t calc_projection_fn_addr() {
        static const auto pattern = "48 8B C4 53 48 81 EC 90 00 00 00 0F 29 70 E8 48 8B D9 F3";
        static auto addr    = FuncRelocation("calc_projection_fn_addr", pattern, 0x817110);
        return addr;
    }

    inline uintptr_t update_views_fn_addr() {
        static const auto pattern = "48 8B C4 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC D8 01";
        static auto addr    = FuncRelocation("update_views_fn_addr", pattern, 0x8b8fc0);
        return addr;
    }

    inline uintptr_t on_begin_frame_fn_addr() {
        static const auto pattern = "E8 ? ? ? ? E8 ? ? ? ? B9 01 00 00 00 86 88 48 04 00 00";
        static auto addr    = InstructionRelocation("on_begin_frame_fn_addr", pattern, 1, 5, 0x15e2810);;
        return addr;
    }

    inline uintptr_t on_gfx_context_copy() {
        static const auto pattern = "0F 10 01 0F 10 49 10 0F 11 81 D0 06 00 00 0F 10 41 20 0F 11 89 E0 06 00 00 0F 10 49 30 0F 11 81 F0 06 00 00 0F 11 89 00 07 00 00 0F";
        static auto addr    = FuncRelocation("on_gfx_context_copy", pattern, 0x8b0550);
        return addr;
    }

    inline uintptr_t g_current_ui_pointer_addr() {
        static const auto pattern = "48 8D 0D ? ? ? ? 48 8B D7 48 8B D8 E8 ? ? ? ? 48 8B CB E8 ? ? ? ? 83 05";
        static auto addr    = InstructionRelocation("g_current_ui_pointer_addr", pattern, 3, 7, 0x62c1bc0);;
        return addr;
    }

    /*
     * get forward vector from camera node for aiming calculations
     */
    inline uintptr_t camera_node_get_forward_addr() {
        static const auto pattern = "0F 10 51 70 48 8B C2 0F 28 1D";
        static auto addr    = FuncRelocation("camera_node_get_forward_addr", pattern, 0x12d6400);
        return addr;
    }

    inline uintptr_t calc_ui_viewport_addr() {
        static const auto pattern = "40 55 48 8D 6C 24 A9 48 81 EC C0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 4F";
        static auto addr    = FuncRelocation("calc_ui_viewport_addr", pattern, 0x853fd0);
        return addr;
    }

    inline uintptr_t g_ui_viewport_addr() {
        static const auto pattern = "48 8D 0D ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? 48 8B C8 45 33 C9";
        static auto addr    = InstructionRelocation("g_ui_viewport_addr", pattern, 3, 7, 0x502b8a8);
        return addr;
    }
}
