//
// Created by sergp on 6/26/2024.
//

#include "EngineRendererModule.h"
#include "EngineEntry.h"
#include "engine/memory/memory_mul.h"
#include <mods/VR.hpp>
#include <safetyhook/easy.hpp>

void EngineRendererModule::InstallHooks()
{
    spdlog::info("Installing EngineRendererModule hooks");

    auto submitMarkerFunct = memory::submit_reflex_marker_fn_addr();
    m_submitMarkerHook = safetyhook::create_inline((void*)submitMarkerFunct, (void*)&EngineRendererModule::submit_nvidia_marker);
    if(!m_submitMarkerHook) {
        spdlog::error("Failed to hook submit nvidia marker");
    }

    auto checkResolutionFunct = memory::check_resolution_fn_addr();
    m_checkResolutionHook = safetyhook::create_inline((void*)checkResolutionFunct, (void*)&EngineRendererModule::checkResolution);
    if(!m_checkResolutionHook) {
        spdlog::error("Failed to hook check resolution");
    }
}


uintptr_t EngineRendererModule::submit_nvidia_marker(uintptr_t pDevice, sdk::NV_LATENCY_MARKER_PARAMS_V1* marker)
{
    static auto instance = Get();
    static     auto        vr            = VR::get();
    auto markerType = marker->markerType;
    auto frameID = marker->frameID;
    static bool engine_notified = false;

    if ((markerType == sdk::NV_LATENCY_MARKER_TYPE::SIMULATION_START || markerType == sdk::NV_LATENCY_MARKER_TYPE::SIMULATION_END) && !engine_notified) {
        vr->m_engine_frame_count = (int)frameID;
        engine_notified = true;
        vr->on_engine_tick(nullptr);
    }
    // Reset notification if markerType is 1
    if (markerType == sdk::NV_LATENCY_MARKER_TYPE::SIMULATION_END) {
        engine_notified = false;
    }

    if(markerType == sdk::NV_LATENCY_MARKER_TYPE::RENDERSUBMIT_START) {
        vr->m_render_frame_count = (int)frameID;
        vr->on_begin_rendering(nullptr);
    }

    if(markerType == sdk::NV_LATENCY_MARKER_TYPE::PRESENT_START) {
        vr->m_presenter_frame_count = (int)frameID;
    }

    if(markerType == sdk::NV_LATENCY_MARKER_TYPE::RENDERSUBMIT_START) {
        g_framework->run_imgui_frame(false);
    }
//    return  instance->m_submitMarkerHook.call<uintptr_t>(pDevice, marker);
    return 0;
}


bool EngineRendererModule::checkResolution(int* outFlags, int maxMessagesToProcess, char filterSpecialMessages)
{
    static auto instance = Get();
    static auto vr = VR::get();
    static auto pRenderW = (int*)(memory::get_render_width_addr());
    static auto pRenderH = (int*)(memory::get_render_height_addr());
    int oldRenderW = *pRenderW;
    int oldRenderH = *pRenderH;
    auto resp = instance->m_checkResolutionHook.call<bool>(outFlags, maxMessagesToProcess, filterSpecialMessages);
    auto vrWidth = vr->get_hmd_width();
    auto vrHeight = vr->get_hmd_height();
    if (vr->is_hmd_active() && oldRenderW > 0 && oldRenderH > 0 && vrWidth > 0 && vrHeight > 0) {
        if(oldRenderW == vrWidth && oldRenderH == vrHeight) {
            *pRenderW = oldRenderW;
            *pRenderH = oldRenderH;
            *outFlags ^= 0x1;
        } else if(*pRenderW != vrWidth || *pRenderH != vrHeight) {
            spdlog::info("Changing render size to {}x{}", vrWidth, vrHeight);
            *pRenderW = vrWidth;
            *pRenderH = vrHeight;
            *outFlags |= 0x1;
        }
    }
    return resp;
}
