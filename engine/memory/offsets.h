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

    inline uintptr_t begin_engine_frame_fn_addr()
    {
        return (uintptr_t) g_mod + 0x10f46f0;
    }

    inline uintptr_t begin_render_frame_fn_addr()
    {
        return (uintptr_t) g_mod + 0x10f4db0;
    }

    inline uintptr_t calc_projection_fn_addr() {
        return (uintptr_t) g_mod + 0x817110;
    }

    inline uintptr_t update_views_fn_addr() {
        return (uintptr_t) g_mod + 0x8b8fc0;
    }

    inline uintptr_t on_camera_get_rotation_quat_addr() {
        return (uintptr_t) g_mod + 0x1236d80;
    }

    inline uintptr_t additional_rotation_quat_addr() {
        return (uintptr_t) g_mod + 0x12d55c0;
    }

    inline uintptr_t camera_update_core_fn_addr() {
        return (uintptr_t) g_mod + 0x1295b40;
    }

    inline uintptr_t onSetScissors_fn_addr() {
        return (uintptr_t) g_mod + 0x7faa60;
    }

    inline uintptr_t on_begin_frame_fn_addr() {
        return (uintptr_t) g_mod + 0x15e2810;
    }

    inline uintptr_t on_gfx_context_copy() {
        return (uintptr_t) g_mod + 0x8b0550;
    }

    inline uintptr_t on_taa_pass_addr() {
        return (uintptr_t) g_mod + 0x17345e0;
    }

    inline uintptr_t on_set_resource_barriers_addr() {
        return (uintptr_t) g_mod + 0x7d3540;
    }

    inline uintptr_t g_current_ui_pointer_addr() {
        return (uintptr_t) g_mod + 0x62c1bc0;
    }

    /*
     * get forward vector from camera node for aiming calculations
     */
    inline uintptr_t camera_node_get_forward_addr() {
        return (uintptr_t) g_mod + 0x12d6400;
    }

    inline uintptr_t calc_ui_viewport_addr() {
        return (uintptr_t) g_mod + 0x853fd0;
    }

    inline uintptr_t g_ui_viewport_addr() {
        return (uintptr_t) g_mod + 0x502b8a8;
    }
}
