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

    static uintptr_t on_begin_engine_frame(struct GlobalContext*);
};
