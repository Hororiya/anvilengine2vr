//
// Created by sergp on 6/26/2024.
//

#include "EngineRendererModule.h"
#include "EngineEntry.h"
#include "engine/memory/offsets.h"
#include <mods/VR.hpp>
#include <safetyhook/easy.hpp>

void EngineRendererModule::InstallHooks()
{
    spdlog::info("Installing EngineRendererModule hooks");

    auto beginEngineFrameFunct = memory::begin_engine_frame_fn_addr();
    m_beginEngineFrameHook = safetyhook::create_inline((void*)beginEngineFrameFunct, (void*)&EngineRendererModule::on_begin_engine_frame);
    if(!m_beginEngineFrameHook) {
        spdlog::error("Failed to hook begin engine frame");
    }
}

uintptr_t EngineRendererModule::on_begin_engine_frame(struct GlobalContext* context)
{
    static auto instance = Get();
    if(g_framework->is_ready()) {
        static auto vr = VR::get();
        vr->m_engine_frame_count++;
        vr->on_engine_tick(nullptr);
        g_framework->enable_engine_thread();
        g_framework->run_imgui_frame(false);
        vr->m_render_frame_count = vr->m_engine_frame_count;
        vr->on_begin_rendering(nullptr);
        vr->m_presenter_frame_count = vr->m_render_frame_count;
    }
    return instance->m_beginEngineFrameHook.call<uintptr_t>(context);
}
