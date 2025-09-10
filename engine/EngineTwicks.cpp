#include "EngineTwicks.h"
#include <engine/memory/memory_mul.h>

namespace EngineTwicks
{
    std::vector<uint8_t> GenerateAspectRatioPatchBytes() {
        // --- The Assembly We Want ---
        // movaps xmm6, xmm3   ; Move the correct aspect ratio (v43 in xmm3)
        //                     ; into the target aspect ratio register (v24 in xmm6).
        //
        // --- Bytecode for this instruction ---
        // 0F 28 F3

        std::vector<uint8_t> patchBytes = { 0x0F, 0x28, 0xF3 };

        // --- Calculate Padding ---
        // The original code block we are replacing starts at RVA 0x991695
        // and ends just before RVA 0x9916CB.
        // Total size = 0x9916CB - 0x991695 = 0x36 bytes (54 bytes).
        // Our patch is 3 bytes long.
        // We need to fill the remaining space with NOPs.
        const size_t originalCodeSize = 54;
        const size_t patchSize = patchBytes.size();
        const size_t nopCount = originalCodeSize - patchSize;

        // A NOP (No-Operation) instruction is 0x90 in x86-64.
        for (size_t i = 0; i < nopCount; ++i) {
            patchBytes.push_back(0x90);
        }

        return patchBytes;
    }

    bool DisableTAA() {
        auto addr = memory::disable_taa_addr();
        const unsigned char patch_taa[] = { 0x41, 0xC7, 0x42, 0x08, 0x00, 0x00, 0x80, 0x7F, 0x41, 0xC7, 0x42, 0x0C, 0x00, 0x00, 0x80, 0x7F, 0x90, 0x90, 0x90, 0x90 };
        return memory::PatchMemory(addr, patch_taa, sizeof(patch_taa));
    }

    bool DisableLetterBox() {
        auto addr = memory::disable_letterbox_addr();
        std::vector<uint8_t> patch = GenerateAspectRatioPatchBytes();
        return memory::PatchMemory(addr, patch);
    }

    bool DisableJitter() {
        auto addr = memory::disable_jitter_addr() + 6;
        const unsigned char patch_jitter[] = { 0x00 };;
        return memory::PatchMemory(addr, patch_jitter, sizeof(patch_jitter));
    }

    bool DisableDof() {
        auto addr = memory::disable_dof_addr();
        const unsigned char patch[] = { 0x45, 0x0F, 0x57, 0xC0, 0xF3, 0x41, 0x0F, 0x5E, 0xD0 };
        return memory::PatchMemory(addr, patch, sizeof(patch));
    }

    bool DisableSharpness() {
        auto addr = memory::disable_sharpness_addr() + 11;
        const unsigned char patch[] = { 0x00, 0x00 };
        return memory::PatchMemory(addr, patch, sizeof(patch));
    }

    bool DisableNvidiaSupportCheck() {
        const unsigned char patch2[] = { 0x90, 0x90 };
        const unsigned char patch6[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        auto addr = memory::submit_marker_0_check_addr();
        auto result = true;
        result &= memory::PatchMemory(addr, patch2, sizeof(patch2));
        if(!result) {
            spdlog::error("Failed to patch Nvidia Reflex Marker 0 support check");
        }
        addr = memory::submit_marker_1_check_addr();
        result &= memory::PatchMemory(addr, patch2, sizeof(patch2));
        if(!result) {
            spdlog::error("Failed to patch Nvidia Reflex Marker 1 support check");
        }
        addr = memory::submit_marker_2_check_addr();
        result &= memory::PatchMemory(addr, patch6, sizeof(patch6));
        if(!result) {
            spdlog::error("Failed to patch Nvidia Reflex Marker 2 support check");
        }
        addr = memory::submit_marker_3_check_addr();
        result &= memory::PatchMemory(addr, patch2, sizeof(patch2));
        if(!result) {
            spdlog::error("Failed to patch Nvidia Reflex Marker 3 support check");
        }
        addr = memory::submit_marker_4_check_addr();
        result &= memory::PatchMemory(addr, patch2, sizeof(patch2));
        if(!result) {
            spdlog::error("Failed to patch Nvidia Reflex Marker 4 support check");
        }
        return result;
    }
};