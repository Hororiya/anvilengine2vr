#include "DLSSModule.h"
#include <engine/memory/memory_mul.h>
#include <engine/models/ModSettings.h>
#include <mods/VR.hpp>
#include <safetyhook/easy.hpp>
#include <spdlog/fmt/bin_to_hex.h>

void DLSSModule::InstallHooks() {
    spdlog::info("Installing DLSSModule hooks");

    auto onNVSDK_NGX_D3D11_EvaluateFeatureFNAddr = memory::nvsdk_ngx_d3d11_evaluate_feature_fn_addr();
    m_onEvaluateFeatureHook = safetyhook::create_inline((void*)onNVSDK_NGX_D3D11_EvaluateFeatureFNAddr, (void*)&DLSSModule::onNVSDK_NGX_D3D11_EvaluateFeature);
    if(!m_onEvaluateFeatureHook) {
        spdlog::error("Failed to create onNVSDK_NGX_D3D11_EvaluateFeature hook");
    }

    auto onNVSDK_NGX_D3D11_CreateFeatureFNAddr = memory::nvsdk_ngx_d3d11_create_feature_fn_addr();
    m_onCreateFeatureHook = safetyhook::create_inline((void*)onNVSDK_NGX_D3D11_CreateFeatureFNAddr, (void*)&DLSSModule::onNVSDK_NGX_D3D11_CreateFeature);
    if(!m_onCreateFeatureHook) {
        spdlog::error("Failed to create onNVSDK_NGX_D3D11_CreateFeature hook");
    }
    auto onNVSDK_NGX_D3D11_ReleaseFeatureFNAddr = memory::nvsdk_ngx_d3d11_release_feature_fn_addr();
    m_onReleaseFeatureHook = safetyhook::create_inline((void*)onNVSDK_NGX_D3D11_ReleaseFeatureFNAddr, (void*)&DLSSModule::onNVSDK_NGX_D3D11_ReleaseFeature);
    if(!m_onReleaseFeatureHook) {
        spdlog::error("Failed to create onNVSDK_NGX_D3D11_ReleaseFeature hook");
    }

    auto onCalcConstantsBufferFNAddr = memory::calc_constants_buffer_root_fn_addr();
    m_onCalcConstantsBufferHook = safetyhook::create_inline((void*)onCalcConstantsBufferFNAddr, (void*)&DLSSModule::onCalcConstantsBuffer);
    if(!m_onCalcConstantsBufferHook) {
        spdlog::error("Failed to create onCalcConstantsBuffer hook");
    }
}


uintptr_t DLSSModule::onCalcConstantsBuffer(sdk::CommonConstantsBufferWrapper* buffer, int type, struct PerspectiveMatrices* matrices, uintptr_t unkPointer)
{
    static auto instance = Get();
    static auto vr = VR::get();
    auto cur_frame = vr->m_render_frame_count;
    static glm::mat4 afr_history_verbose[2][6] {
        {glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f) },
        {glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f) }
    };
    auto result = instance->m_onCalcConstantsBufferHook.call<uintptr_t>(
        buffer,
        type,
        matrices,
        unkPointer
    );
    if(ModSettings::g_internalSettings.nvidiaMotionVectorFix) {
        afr_history_verbose[cur_frame % 2][type] = buffer->pasViewProjS;
        buffer->pasViewProjS = afr_history_verbose[(cur_frame - 1) % 2][type];
        typedef uintptr_t* (*func_t)(sdk::CommonConstantsBufferWrapper*, uintptr_t, uintptr_t);
        static auto recalc_buffer_fn = (func_t) ( memory::recalc_constants_buffer_fn_addr());
        recalc_buffer_fn(buffer, 0, 0);
    }
    return result;
}


uintptr_t DLSSModule::onNVSDK_NGX_D3D11_EvaluateFeature(ID3D11DeviceContext* InDevCtx, const NVSDK_NGX_Handle* InFeatureHandle, const NVSDK_NGX_Parameter* InParameters,
                                                   PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    static auto instance = Get();
    static     auto        vr            = VR::get();
    if(ModSettings::g_internalSettings.nvidiaMotionVectorFix && vr->is_hmd_active() && vr->m_render_frame_count % 2 == 1 && instance->m_afr_handle_ptr) {
        return  instance->m_onEvaluateFeatureHook.call<uintptr_t>(
            InDevCtx,
            instance->m_afr_handle_ptr,
            InParameters,
            InCallback
        );
    }
    return  instance->m_onEvaluateFeatureHook.call<uintptr_t>(
        InDevCtx,
        InFeatureHandle,
        InParameters,
        InCallback
    );

}

uintptr_t DLSSModule::onNVSDK_NGX_D3D11_CreateFeature(ID3D11DeviceContext* InDevCtx, int InFeatureID, const NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)

{
    static auto instance = Get();
    {
        if(instance->m_afr_handle_ptr) {
            spdlog::warn("AFR handle already exists, skipping creation");
            auto result = instance->m_onReleaseFeatureHook.call<uintptr_t>(
                instance->m_afr_handle_ptr
            );
            if( result != 0x1) {
                spdlog::error("Failed to release AFR handle: {}", result);
            }
            instance->m_afr_handle_ptr = nullptr;
        }
        auto result= instance->m_onCreateFeatureHook.call<uintptr_t>(
            InDevCtx,
            InFeatureID,
            InParameters,
            &instance->m_afr_handle_ptr
        );
        if (result != 0x1) {
            spdlog::error("Failed to create AFR handle: {}", result);
            instance->m_afr_handle_ptr = nullptr;
        }
    }
    auto result =  instance->m_onCreateFeatureHook.call<uintptr_t>(
        InDevCtx,
        InFeatureID,
        InParameters,
        OutHandle
    );
    spdlog::info("onNVSDK_NGX_D3D11_CreateFeature called with result: {}", result);
    return result;
}

uintptr_t DLSSModule::onNVSDK_NGX_D3D11_ReleaseFeature(NVSDK_NGX_Handle* InHandle) {
    static auto instance = Get();
    if(instance->m_afr_handle_ptr) {
        auto result = instance->m_onReleaseFeatureHook.call<uintptr_t>(
            instance->m_afr_handle_ptr
        );
        instance->m_afr_handle_ptr = nullptr;
        if (result != 0x1) {
            spdlog::error("Failed to release AFR handle: {}", result);
        }
    }

    return  instance->m_onReleaseFeatureHook.call<uintptr_t>(
        InHandle
    );
}