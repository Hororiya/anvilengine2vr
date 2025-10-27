#include "EngineRendererModule.h"
#include "EngineEntry.h"
#include "engine/memory/offsets.h"
#include <mods/VR.hpp>
#include <safetyhook/easy.hpp>
#include <utility/ScopeProfiler.h>

namespace sdk {
    struct Task {
        char     pad_0000[0x1A8];
        const char* pName;
    };
}

void EngineRendererModule::InstallHooks()
{
    spdlog::info("Installing EngineRendererModule hooks");

//    auto beginEngineFrameFunct = memory::on_begin_frame_fn_addr();
//    m_beginEngineFrameHook     = safetyhook::create_inline((void*)beginEngineFrameFunct, (void*)&EngineRendererModule::on_begin_engine_frame);
//    if (!m_beginEngineFrameHook) {
//        spdlog::error("Failed to hook begin engine frame");
//    }
//    auto begin_render_frame_fn_addr = memory::on_frame_fn_addr();
//    m_beginRenderFrameHook          = safetyhook::create_inline((void*)begin_render_frame_fn_addr, (void*)&EngineRendererModule::on_begin_render_frame);
//    if (!m_beginRenderFrameHook) {
//        spdlog::error("Failed to hook begin render frame");
//    }
    auto task_fn_addr = memory::on_task_fn_addr();
    m_taskHook       = safetyhook::create_inline((void*)task_fn_addr, (void*)&EngineRendererModule::on_task);
    if (!m_taskHook) {
        spdlog::error("Failed to hook task");
    }
}

//uintptr_t EngineRendererModule::on_begin_engine_frame(uintptr_t a1 , uintptr_t a2)
//{
//    SCOPE_PROFILER();
//    static auto instance = Get();
//    if (g_framework->is_ready()) {
//        static auto vr = VR::get();
//        vr->m_engine_frame_count++;
//        vr->on_engine_tick(nullptr);
//        g_framework->enable_engine_thread();
//        g_framework->run_imgui_frame(false);
//    }
//    return instance->m_beginEngineFrameHook.call<uintptr_t>(a1,a2);
//}
//


//uintptr_t EngineRendererModule::on_begin_render_frame(uintptr_t a1 , uintptr_t a2, uintptr_t a3)
//{
//    static auto instance = Get();
//    SCOPE_PROFILER();
//    if (g_framework->is_ready()) {
//        static auto vr = VR::get();
//        vr->m_engine_frame_count++;
//        vr->on_engine_tick(nullptr);
//        g_framework->enable_engine_thread();
//        g_framework->run_imgui_frame(false);
//    }
//    if (g_framework->is_ready()) {
//        static auto vr           = VR::get();
//        vr->m_render_frame_count = vr->m_engine_frame_count;
//        vr->on_begin_rendering(nullptr);
//        auto result                 = instance->m_beginRenderFrameHook.call<uintptr_t>(a1, a2, a3);
//        vr->m_presenter_frame_count = vr->m_render_frame_count;
//        return result;
//    }
//    return instance->m_beginRenderFrameHook.call<uintptr_t>(a1, a2, a3);
//}

uintptr_t original_on_task(uintptr_t a1) {

    uintptr_t v2; // rcx
    uintptr_t result; // rax
    v2 = a1 + 256;
    if ( *(uintptr_t *)v2 || *(uintptr_t *)(v2 + 8) )
        (*(void (__fastcall **)(__int64, uintptr_t ))(v2 + 32))(v2, *(uintptr_t *)v2);
    result = (*(__int64 (__fastcall **)(__int64, uintptr_t ))(a1 + 480))(a1 + 0x1C0, *(uintptr_t *)(a1 + 0x1C0));
    if ( *(uintptr_t *)(a1 + 0x128) || *(uintptr_t *)(a1 + 304) )
        return (*(__int64 (__fastcall **)(__int64, uintptr_t ))(a1 + 328))(a1 + 0x128, *(uintptr_t *)(a1 + 0x128));
    return result;
}

uintptr_t EngineRendererModule::on_task(uintptr_t a1)
{
    auto task = reinterpret_cast<sdk::Task*>(a1);
    static auto vr           = VR::get();
    static uintptr_t pBeginFrame = 0;
    static uintptr_t pViewPreRender = 0;
    if(!pBeginFrame && task && task->pName && strcmp(task->pName, "BeginFrame") == 0) {
        pBeginFrame = a1;
    }
    else if(!pViewPreRender && task && task->pName && strcmp(task->pName, "ViewPreRender") == 0) {
        pViewPreRender = a1;
    }

    if(pBeginFrame && pBeginFrame == a1) {
        if (g_framework->is_ready()) {
            vr->m_engine_frame_count++;
            vr->on_engine_tick(nullptr);
            g_framework->enable_engine_thread();
            g_framework->run_imgui_frame(false);

            vr->m_render_frame_count = vr->m_engine_frame_count;
            vr->on_begin_rendering(nullptr);
        }
    } else if(pViewPreRender && pViewPreRender == a1) {
        vr->m_presenter_frame_count = vr->m_render_frame_count;
    }
    return original_on_task(a1);
}
