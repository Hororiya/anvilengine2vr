#include "EngineRendererModule.h"
#include "EngineEntry.h"
#include "engine/memory/offsets.h"
#include <mods/VR.hpp>
#include <safetyhook/easy.hpp>
#include <utility/ScopeProfiler.h>

void EngineRendererModule::InstallHooks()
{
    spdlog::info("Installing EngineRendererModule hooks");

    auto beginEngineFrameFunct = memory::on_begin_frame_fn_addr();
    m_beginEngineFrameHook     = safetyhook::create_inline((void*)beginEngineFrameFunct, (void*)&EngineRendererModule::on_begin_engine_frame);
    if (!m_beginEngineFrameHook) {
        spdlog::error("Failed to hook begin engine frame");
    }
    auto begin_render_frame_fn_addr = memory::begin_render_frame_fn_addr();
    m_beginRenderFrameHook          = safetyhook::create_inline((void*)begin_render_frame_fn_addr, (void*)&EngineRendererModule::on_begin_render_frame);
    if (!m_beginRenderFrameHook) {
        spdlog::error("Failed to hook begin render frame");
    }
}

uintptr_t EngineRendererModule::on_begin_engine_frame()
{
    SCOPE_PROFILER();
    static auto instance = Get();
    if (g_framework->is_ready()) {
        static auto vr = VR::get();
        vr->m_engine_frame_count++;
        vr->on_engine_tick(nullptr);
        g_framework->enable_engine_thread();
        g_framework->run_imgui_frame(false);
    }
    return instance->m_beginEngineFrameHook.call<uintptr_t>();
}

uintptr_t EngineRendererModule::on_begin_render_frame(struct GlobalContext* context)
{
    static auto instance = Get();
    SCOPE_PROFILER();
    if (g_framework->is_ready()) {
        static auto vr           = VR::get();
        vr->m_render_frame_count = vr->m_engine_frame_count;
        vr->on_begin_rendering(nullptr);
        auto result                 = instance->m_beginRenderFrameHook.call<uintptr_t>(context);
        vr->m_presenter_frame_count = vr->m_render_frame_count;
        return result;
    }
    return instance->m_beginRenderFrameHook.call<uintptr_t>(context);
    ;
}
