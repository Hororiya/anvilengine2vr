#pragma once

#include <safetyhook/inline_hook.hpp>

namespace sdk
{
    struct Task;
} // namespace sdk

class EngineRendererModule
{
public:
    void InstallHooks();

    inline static EngineRendererModule* Get()
    {
        static auto instance(new EngineRendererModule);
        return instance;
    }
private:
    EngineRendererModule()  = default;
    ~EngineRendererModule() = default;

    safetyhook::InlineHook m_beginEngineFrameHook{};
    safetyhook::InlineHook m_beginRenderFrameHook{};
    safetyhook::InlineHook m_taskHook{};

    static uintptr_t on_begin_engine_frame(uintptr_t, uintptr_t);
    static uintptr_t on_begin_render_frame(uintptr_t, uintptr_t, uintptr_t);
    static uintptr_t on_task(uintptr_t/*, sdk::Task* task*/);
};
