#pragma once

namespace memory
{
    extern uintptr_t InstructionRelocation(const char* hook_name, const char* pattern, UINT offset_begin, UINT instruction_size, uintptr_t static_offset);
    extern uintptr_t FuncRelocation(const char* hook_name, const char* pattern, uintptr_t static_offset);
    extern uintptr_t VTable(const char* hook_name, const char* table, uintptr_t static_offset);
    extern bool      PatchMemory(uintptr_t address, const std::vector<uint8_t>& patchBytes);
    extern bool      PatchMemory(uintptr_t address, const unsigned char* patch, size_t patchSize);

    extern HMODULE g_mod;
    extern size_t  mod_size;
    extern size_t  mod_end;

    inline uintptr_t disable_taa_addr()
    {
        static auto pattern = "F3 41 0F 11 42 08 F3 0F 10 ? ? ? ? ? F3 41 0F 11 4A 0C";
        static auto addr    = FuncRelocation("disable_taa_addr", pattern, 0x52ece6);
        return addr;
    }

    inline uintptr_t disable_jitter_addr()
    {
        static auto pattern = "C6 83 A0 03 00 00 01";
        static auto addr    = FuncRelocation("disable_jitter_addr", pattern, 0x4d2d18);
        return addr;
    }

    inline uintptr_t disable_dof_addr()
    {
        static auto pattern = "F3 44 0F 59 C7 F3 0F 59 D7";
        static auto addr    = FuncRelocation("disable_dof_addr", pattern, 0x4116da);
        return addr;
    }

    inline uintptr_t disable_sharpness_addr()
    {
        static auto pattern = "0F 28 C2 C7 05 ? ? ? 00 00 00 80 3F";
        static auto addr    = FuncRelocation("disable_sharpness_addr", pattern, 0x412767);
        return addr;
    }

    inline uintptr_t disable_letterbox_addr()
    {
        static auto pattern = "77 34 F3 0F 10 35";
        static auto addr    = FuncRelocation("disable_letterbox_addr", pattern, 0x991695);
        return addr;
    }

    inline uintptr_t calc_projection_fn_addr()
    {
        static auto pattern = "4C 8B DC 49 89 5B 18 49 89 73 20 55 57 41 54 41 55 41 56 49 8D 6B";
        static auto addr    = FuncRelocation("calc_projection_fn_addr", pattern, 0x4e74c0);
        return addr;
    }

    inline uintptr_t update_views_fn_addr()
    {
        static auto pattern = "48 8B C4 57 48 81 EC A0 00 00 00 48 89";
        static auto addr    = FuncRelocation("update_views_fn_addr", pattern, 0x4c1540);
        return addr;
    }

    inline uintptr_t submit_reflex_marker_fn_addr()
    {
        static auto pattern = "48 89 74 24 10 57 48 83 EC 20 48 8B FA 48 8B F1 F0 83 05 ? ? ? ? ? 33 D2 33 C9 E8 ? ? ? ? 85 C0 0F 85 87 00 00 00 48 89 5C 24 30 48 8B 1D ? ? ? ? 48 85 DB 75 29 48 8B 05 ? ? ? ? 48 85 C0 74 16 B9 05 4C";
        static auto addr    = FuncRelocation("submit_reflex_marker_fn_addr", pattern, 0x10650);
        return addr;
    }

    inline uintptr_t check_resolution_fn_addr()
    {
        static auto pattern = "4C 8B DC 55 41 54 41 56 41";
        static auto addr    = FuncRelocation("check_resolution_fn_addr", pattern, 0x98fbe0);
        return addr;
    }

    inline uintptr_t get_render_width_addr()
    {
        static auto pattern = "89 05 ? ? ? ? 8B 45 13";
        static auto addr    = InstructionRelocation("get_render_width_addr", pattern, 2, 6, 0x504c398);
        return addr;
    }

    inline uintptr_t get_render_height_addr()
    {
        return get_render_width_addr() + 4;
    }

    inline uintptr_t recalc_constants_buffer_fn_addr()
    {
        static auto pattern = "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 56 41 57 48 8D 6C 24 90";
        static auto addr    = FuncRelocation("recalc_constants_buffer_fn_addr", pattern, 0x4d3710);
        return addr;
    }

    inline uintptr_t nvsdk_ngx_d3d11_evaluate_feature_fn_addr()
    {
        static auto pattern = "48 83 EC 38 48 8B C1 48 8B 0D ? ? ? ? 48 85 C9 75 0A";
        static auto addr    = FuncRelocation("nvsdk_ngx_d3d11_evaluate_feature_fn_addr", pattern, 0x3dfd60);
        return addr;
    }

    inline uintptr_t nvsdk_ngx_d3d11_create_feature_fn_addr()
    {
        static auto pattern = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 48 8B 1D ? ? ? ? 49 8B F9 49 8B F0 8B";
        static auto addr    = FuncRelocation("nvsdk_ngx_d3d11_create_feature_fn_addr", pattern, 0x3dfc20);
        return addr;
    }

    inline uintptr_t nvsdk_ngx_d3d11_release_feature_fn_addr()
    {
        static auto pattern = "E8 ? ? ? ? 33 C0 49 89 87 C8 00 00 00";
        static auto addr    = InstructionRelocation("nvsdk_ngx_d3d11_release_feature_fn_addr", pattern, 1, 5, 0x3e00e0);
        return addr;
    }

    inline uintptr_t calc_constants_buffer_root_fn_addr()
    {
        static auto pattern = "4C 8B DC 55 53 57 49 8D AB 18 FF";
        static auto addr    = FuncRelocation("calc_constants_buffer_root_fn_addr", pattern, 0x4d25b0);
        return addr;
    }

    // 2 noop
    inline uintptr_t submit_marker_0_check_addr() {
        static auto pattern = "75 2B C7 85 C0 02 00 00 58 00 01 00";
        static auto addr    = FuncRelocation("submit_marker_0_check_addr", pattern, 0x407b67);
        return addr;
    }

    // 2 noop
    inline uintptr_t submit_marker_1_check_addr() {
        static auto pattern = "75 34 C7 85 80 03 00 00 58 00 01 00";
        static auto addr    = FuncRelocation("submit_marker_1_check_addr", pattern, 0x409959);
        return addr;
    }

    // 6 noop
    inline uintptr_t submit_marker_2_check_addr() {
        static auto pattern = "0F 85 CA 02 00 00";
        static auto addr    = FuncRelocation("submit_marker_2_check_addr", pattern, 0x9934de);
        return addr;
    }

    // 2 noop
    inline uintptr_t submit_marker_3_check_addr() {
        static auto pattern = "75 32 48 8B 0D ? ? ? ? 48 8D 94 24 90 00 00 00";
        static auto addr    = FuncRelocation("submit_marker_3_check_addr", pattern, 0x993524);
        return addr;
    }

    // 2 noop
    inline uintptr_t submit_marker_4_check_addr() {
        static auto pattern = "75 32 48 8B 0D ? ? ? ? 48 8D 94 24 F0 00 00 00";
        static auto addr    = FuncRelocation("submit_marker_4_check_addr", pattern, 0x993583);
        return addr;
    }

}
