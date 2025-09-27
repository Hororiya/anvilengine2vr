//#pragma once
//#include <d3d12.h>
//#include <glm/fwd.hpp>
//#include <mods/vr/d3d12/ComPtr.hpp>
//#include <safetyhook/inline_hook.hpp>
//
//namespace sdk {
//    struct TaaPass;
//    struct TaaContext;
//    struct RenderContext;
//    struct GlobalResourceManager;
//    struct GfxContext;
//}
//using Microsoft::WRL::ComPtr;
//
//
//class AERFixModule
//{
//public:
//    void InstallHooks();
//
//    inline static AERFixModule* Get()
//    {
//        static auto instance(new AERFixModule);
//        return instance;
//    }
//
//private:
//    AERFixModule()  = default;
//    ~AERFixModule() = default;
//
//    safetyhook::InlineHook m_onTAAPassHook{};
//    safetyhook::InlineHook m_onSetResourceBarriersHook{};
//    safetyhook::InlineHook m_onCopyGfxContext{};
//
//    ComPtr<ID3D12Resource> m_pastBuffer[4][4];
//
//
//    static uintptr_t onTAAPass(sdk::TaaPass* pass, sdk::TaaContext* context, uintptr_t);
//    static void onSetResourceBarriers(sdk::GlobalResourceManager* context, sdk::RenderContext* barriers);
//    static uintptr_t onCopyGfxContext(sdk::GfxContext* context, glm::mat4* viewMatrix);
//
//    static bool      ValidateResource(ID3D12Resource* source, ComPtr<ID3D12Resource> pPtr[4]);
//    void SwapBuffer(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* originalBuffer, D3D12_RESOURCE_STATES state, int index);
//    static void CopyResource(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* pSrcResource, ID3D12Resource* pDstResource, D3D12_RESOURCE_STATES srcState, D3D12_RESOURCE_STATES dstState);
//
//
//};
//
