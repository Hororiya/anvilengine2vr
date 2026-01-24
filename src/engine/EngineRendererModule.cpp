#include "EngineRendererModule.h"
#include "engine/memory/offsets.h"
#include <mods/VR.hpp>
#include <safetyhook/easy.hpp>

namespace sdk {
    struct Task {
        char     pad_0000[0x1A8];
        const char* pName;
    };
}

void EngineRendererModule::InstallHooks()
{
    spdlog::info("Installing EngineRendererModule hooks");
    auto task_fn_addr = memory::on_task_fn_addr();
    m_taskHook       = safetyhook::create_inline((void*)task_fn_addr, (void*)&EngineRendererModule::on_task);
    if (!m_taskHook) {
        spdlog::error("Failed to hook task");
    }
}

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
            g_framework->enable_engine_thread();
            g_framework->run_imgui_frame(false);
            vr->update_hmd_state(vr->m_engine_frame_count);
            vr->m_render_frame_count = vr->m_engine_frame_count - 1;
            vr->on_begin_rendering(vr->m_render_frame_count);
        }
    } else if(pViewPreRender && pViewPreRender == a1) {
        vr->m_presenter_frame_count = vr->m_render_frame_count;
    }
    return original_on_task(a1);
}
