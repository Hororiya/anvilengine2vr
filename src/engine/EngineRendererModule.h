#pragma once

#include <safetyhook/inline_hook.hpp>
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

    static uintptr_t on_begin_engine_frame();
    static uintptr_t on_begin_render_frame(struct GlobalContext*);
};
