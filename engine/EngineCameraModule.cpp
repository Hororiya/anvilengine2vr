#include "EngineCameraModule.h"
#include "engine/memory/memory_mul.h"
#include <engine/models/ModSettings.h>
#include <mods/VR.hpp>
#include <safetyhook/easy.hpp>

void EngineCameraModule::InstallHooks()
{
    spdlog::info("Installing EngineCameraModule hooks");

    auto onCalcProjectionFN = memory::calc_projection_fn_addr();
    m_onCalcProjection      = safetyhook::create_inline((void*)onCalcProjectionFN, (void*)&EngineCameraModule::onCalcProjection);
    if (!m_onCalcProjection) {
        spdlog::error("Failed to hook m_onCalcProjection");
    }

    auto calcFinalViewFn = memory::update_views_fn_addr();
    m_onCalcFinalView = safetyhook::create_inline((void*)calcFinalViewFn, (void*)&EngineCameraModule::onCalcFinalView);
    if (!m_onCalcFinalView) {
        spdlog::error("Failed to hook m_onCalcFinalView");
    }
}

sdk::Matrix4x4* EngineCameraModule::onCalcProjection(sdk::CameraClient* a1, float aspectRatio, uint8_t bUseCustomAspectRatio, float customAspectRatioFactor, float nearZ,
                                                     float farZ, float viewportWidth, float viewportHeight, uint8_t bSomeFlag, sdk::Matrix4x4* outProjMatrix,
                                                     sdk::Matrix4x4* outInvProjMatrix, float* outTanHalfFovX, float* outTanHalfFovY, sdk::Matrix4x4* outJitterMatrix)
{
    static auto instance = EngineCameraModule::Get();

    auto result = instance->m_onCalcProjection.call<sdk::Matrix4x4*>(a1, aspectRatio, bUseCustomAspectRatio, customAspectRatioFactor, nearZ, farZ, viewportWidth, viewportHeight,
                                                                     bSomeFlag, outProjMatrix, outInvProjMatrix, outTanHalfFovX, outTanHalfFovY, outJitterMatrix);

    static auto vr = VR::get();
    if (!vr->is_hmd_active()) {
        return result;
    }
    if (a1 && a1->pViewport->orpho) {
        auto scale_factor = ModSettings::g_UISettings.hudScale;
        outProjMatrix->row[0][0] *= scale_factor;
        outProjMatrix->row[1][1] *= scale_factor;
        outProjMatrix->row[3][1] += ModSettings::g_UISettings.hudVerticalOffset;
        auto invViewProj  = glm::inverse(*(glm::mat4*)outProjMatrix);
        *outInvProjMatrix = *(sdk::Matrix4x4*)&invViewProj;
    }
    else {
        vr->m_nearz       = std::fabs(nearZ);
        vr->m_farz        = std::fabs(farZ);
        auto proj         = vr->get_current_projection_matrix();
        *outTanHalfFovX   = 1.0f / proj[0][0];
        *outTanHalfFovY   = 1.0f / proj[1][1];
        *outProjMatrix    = *(sdk::Matrix4x4*)&proj;
        auto invProj      = glm::inverse(proj);
        *outInvProjMatrix = *(sdk::Matrix4x4*)&invProj;
        static glm::mat4 identity{ 1.0f };
        *outJitterMatrix = *(sdk::Matrix4x4*)&identity;
    }
    return result;
}

void EngineCameraModule::onCalcFinalView(sdk::CameraBlender* a1)
{
    static auto instance = EngineCameraModule::Get();
    instance->m_onCalcFinalView.call<void>(a1);
    static auto vr = VR::get();
    if (!vr->is_hmd_active()) {
        return;
    }
    auto        eye           = vr->get_current_eye_transform();
    static auto hmd_transform = vr->get_transform(0);
    if (vr->get_current_render_eye() == VRRuntime::Eye::LEFT) {
        hmd_transform = vr->get_transform(0);
    }
    auto transform = *(glm::mat4*)&a1->view * hmd_transform * eye;
    a1->view       = *(sdk::Matrix4x4*)&transform;
}
