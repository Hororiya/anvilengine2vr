#pragma once
#include <d3d11.h>
#include <glm/detail/type_mat4x4.hpp>
#include <glm/fwd.hpp>
#include <safetyhook/inline_hook.hpp>
#include <utility/PointerHook.hpp>
#include <utility/VtableHook.hpp>

namespace
{
    typedef struct NVSDK_NGX_Handle
    {
        unsigned int Id;
    } NVSDK_NGX_Handle;

    typedef void* PFN_NVSDK_NGX_ProgressCallback;
    typedef void* NVSDK_NGX_Parameter;
} // namespace

namespace sdk {
    struct CommonConstantsBufferWrapper {
        glm::mat4 proj;
        glm::mat4 View;
        glm::mat4 InvProj;
        glm::mat4 InvView;
        glm::mat4 pasViewProjS;
        float data2[32];
        glm::mat4 InvViewProjNoZ;
        float data3[69];
        int bufferType;
        float data4[142];
        struct CommonConstantsBuffer *rowMajorBuffer;
    };
}

class DLSSModule
{
public:
    void InstallHooks();

    inline static DLSSModule* Get()
    {
        static auto instance(new DLSSModule);
        return instance;
    }
private:
    DLSSModule()  = default;
    ~DLSSModule() = default;

    safetyhook::InlineHook m_onEvaluateFeatureHook{};
    safetyhook::InlineHook m_onCreateFeatureHook{};
    safetyhook::InlineHook m_onReleaseFeatureHook{};
    safetyhook::InlineHook m_onCalcConstantsBufferHook{};

//    NVSDK_NGX_Handle m_afr_handle{ 2u };
    NVSDK_NGX_Handle* m_afr_handle_ptr{};
    static uintptr_t onNVSDK_NGX_D3D11_EvaluateFeature(ID3D11DeviceContext* InDevCtx, const NVSDK_NGX_Handle* InFeatureHandle, const NVSDK_NGX_Parameter* InParameters,
                                                       PFN_NVSDK_NGX_ProgressCallback InCallback = NULL);
    static uintptr_t onNVSDK_NGX_D3D11_CreateFeature(ID3D11DeviceContext *InDevCtx, int InFeatureID, const NVSDK_NGX_Parameter *InParameters, NVSDK_NGX_Handle **OutHandle);
    static uintptr_t onNVSDK_NGX_D3D11_ReleaseFeature(NVSDK_NGX_Handle* InHandle);
    static uintptr_t onCalcConstantsBuffer(sdk::CommonConstantsBufferWrapper*, int type, struct PerspectiveMatrices* matrices, uintptr_t unkPointer);
};
