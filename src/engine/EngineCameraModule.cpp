#include "EngineCameraModule.h"
#include "EngineEntry.h"
#include "engine/memory/offsets.h"
#include <ModConfig.h>
#include <ModSettings.h>
#include <mods/VR.hpp>
#include <safetyhook/easy.hpp>
#include <sdk/AnvilEngine.h>
#include <utility/ScopeProfiler.h>

namespace sdk {
    struct Viewport {
        float left;
        float top;
        float right;
        float bottom;
    };

}

bool isShowingUi()
{
    static auto g_current_ui = (uintptr_t**)memory::g_current_ui_pointer_addr();
    static auto g_hud_ui = (uintptr_t*)memory::g_hud_overlay_pointer();
    return *g_current_ui != g_hud_ui;
}

void EngineCameraModule::InstallHooks()
{
    spdlog::info("Installing EngineCameraModule hooks");
    //
    auto onCalcProjectionFN = memory::calc_projection_fn_addr();
    m_onCalcProjectionHook  = safetyhook::create_inline((void*)onCalcProjectionFN, (void*)&EngineCameraModule::onCalcProjection);
    if (!m_onCalcProjectionHook) {
        spdlog::error("Failed to hook m_onCalcProjection");
    }
    auto onCalcMvpFN = memory::on_calc_mvp_fn_addr();
    m_onCalcMvpHook  = safetyhook::create_inline((void*)onCalcMvpFN, (void*)&EngineCameraModule::onCalcMvp);
    if (!m_onCalcMvpHook) {
        spdlog::error("Failed to hook m_onCalcMvp");
    }

    auto onCameraGetForwardHookFn = memory::camera_node_get_forward_addr();
    m_onCameraGetForwardHook      = safetyhook::create_inline((void*)onCameraGetForwardHookFn, (void*)&EngineCameraModule::onCameraGetForwardHook);
    if (!m_onCameraGetForwardHook) {
        spdlog::error("Failed to hook m_onCameraGetForwardHook");
    }

    auto calcUIViewportFn  = memory::calc_ui_viewport_addr();
    m_onCalcUIViewportHook = safetyhook::create_inline((void*)calcUIViewportFn, (void*)&EngineCameraModule::onCalcUIViewportHook);
    if (!m_onCalcUIViewportHook) {
        spdlog::error("Failed to hook m_onCalcUIViewportHook");
    }
}


uintptr_t EngineCameraModule::onCalcProjection( glm::mat4 *outProjMatrix,
                                          float nearPlane,
                                          float farPlane,
                                          float fovy,
                                          float aspectRatio,
                                          float *pInputVector)
{
    SCOPE_PROFILER();
    static auto instance = Get();
    static auto vr           = VR::get();
    auto result =  instance->m_onCalcProjectionHook.call<uintptr_t>( outProjMatrix, nearPlane, farPlane, fovy, aspectRatio, pInputVector);
    if (vr->is_hmd_active() && farPlane > 1201.f) {
        auto      frustum    = vr->get_runtime()->frustums[0];
        float     sum_rl     = (frustum[1] + frustum[0]);
        float     sum_tb     = (frustum[2] + frustum[3]);
        float     inv_rl     = (1.0f / (frustum[1] - frustum[0]));
        float     inv_tb     = (1.0f / (frustum[2] - frustum[3]));
        auto projection = Matrix4x4f {
            2.0f*inv_rl,   0.0f,          0.0f,                                              0.0f,
            0.0f,          2.0f*inv_tb,   0.0f,                                              0.0f,
            sum_rl*inv_rl, sum_tb*inv_tb, farPlane/(nearPlane-farPlane),                    -1.0f,
            0.0f,          0.0f,          (nearPlane*farPlane)/(nearPlane-farPlane),         0.0f
        };
        *outProjMatrix       = projection;
    }
    return result;
}


 const glm::mat4 Y_UP_TO_Z_UP_BASIS(1.0f, 0.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 1.0f, 0.0f,
                                   0.0f, -1.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 0.0f, 1.0f);

 const glm::mat4 INV_UP_TO_Z_UP_BASIS = glm::inverse(Y_UP_TO_Z_UP_BASIS);

 float roundUp(float val) {
    return (float)((int)(((int)val + 1) & 0xFFFFFFFE));
 }

uintptr_t EngineCameraModule::onCalcMvp(sdk::Camera* camera, uintptr_t a2, float* pNormalizedViewportRect, float* pOutRenderDimensions, float* pOutAbsoluteViewport,
                                        float* pOutScaledViewport, float* pOutViewportScale, float* viewport, glm::mat4* pOutViewMatrix, glm::mat4* pOutViewProjectionMatrix,
                                        glm::mat4* pOutProjectionMatrix, glm::mat4* pOutInverseViewMatrix, glm::mat4* pOutInverseProjectionMatrix, glm::mat4* pOutInvViewProjMatrix,
                                        float* fovY, float* pAspectRatio, float nearPlane, float farPlane, uintptr_t pFrustum, uintptr_t a20)
{
    SCOPE_PROFILER();
    static auto instance     = Get();
    static auto vr           = VR::get();
    static auto g_current_ui = (uintptr_t*)memory::g_current_ui_pointer_addr();
    if (vr->is_hmd_active() && !isShowingUi()) {
        auto        eye           = vr->get_current_eye_transform(true);
        static auto hmd_transform = vr->get_transform(0);
        if (vr->get_current_render_eye() == VRRuntime::Eye::RIGHT) {
            hmd_transform = vr->get_transform(0);
        }
        auto rotation_offset = glm::mat4_cast(glm::quat(vr->get_rotation_offset()));
        rotation_offset[3]   = vr->get_standing_origin();
        auto transform       = Y_UP_TO_Z_UP_BASIS * rotation_offset * hmd_transform * eye * INV_UP_TO_Z_UP_BASIS;
        *pOutViewMatrix       = *pOutViewMatrix * transform;
    }

    auto result = instance->m_onCalcMvpHook.call<uintptr_t>(camera, a2, pNormalizedViewportRect, pOutRenderDimensions, pOutAbsoluteViewport, pOutScaledViewport,
                                               pOutViewportScale, viewport, pOutViewMatrix, pOutViewProjectionMatrix, pOutProjectionMatrix, pOutInverseViewMatrix,
                                               pOutInverseProjectionMatrix, pOutInvViewProjMatrix, fovY, pAspectRatio, nearPlane, farPlane, pFrustum, a20);
    if (vr->is_hmd_active()) {
        float offX = pOutAbsoluteViewport[0] * 2.0f;
        float offY = pOutAbsoluteViewport[1] * 2.0f;
        pOutAbsoluteViewport[2] += offX;
        pOutAbsoluteViewport[3] += offY;
        pOutAbsoluteViewport[0] = 0.0f;
        pOutAbsoluteViewport[1] = 0.0f;
        pOutScaledViewport[0] = 0.0f;
        pOutScaledViewport[1] = 0.0f;
        pOutScaledViewport[2] = roundUp(pOutAbsoluteViewport[2]);
        pOutScaledViewport[3] = roundUp(pOutAbsoluteViewport[3]);

        pOutRenderDimensions[2] = pOutAbsoluteViewport[2];
        pOutRenderDimensions[3] = pOutAbsoluteViewport[3];
    }

    return result;
}

glm::vec4* EngineCameraModule::onCameraGetForwardHook(sdk::CameraNode* node, glm::vec4* forward)
{
    SCOPE_PROFILER();
    static auto vr       = VR::get();
    static auto instance = Get();
    auto        result   = instance->m_onCameraGetForwardHook.call<glm::vec4*>(node, forward);
    if (vr->is_hmd_active()) {
        static auto hmd_rotation = glm::normalize(glm::quat(vr->get_rotation(0)));
        if (vr->get_current_render_eye() == VRRuntime::Eye::RIGHT) {
            hmd_rotation = glm::normalize(glm::quat(vr->get_rotation(0)));
        }
        auto rotation = vr->get_rotation_offset() * hmd_rotation;
        rotation      = glm::quat{ rotation.w, rotation.x, -rotation.z, rotation.y };
        auto prevRotation = (glm::quat*)((uintptr_t ) node + 0x70);
        *forward      = *prevRotation * rotation * glm::vec4{ 0.0, 1.0, 0.0, 0.0 };
    }
    return result;
}

 void EngineCameraModule::onCalcUIViewportHook()
{
    static auto instance = Get();
    static auto vr       = VR::get();
    instance->m_onCalcUIViewportHook.call<void>();
    static auto ui_vp = reinterpret_cast<sdk::Viewport*>(memory::g_ui_viewport_addr());
    if (vr->is_hmd_active()) {
        float totalWidth  = ui_vp->left * 2.f + ui_vp->right;
        float totalHeight = ui_vp->top * 2.f + ui_vp->bottom;
        float scaleX      = ModSettings::g_ac_odyssey_ui_settings.hudScaleX;
        float scaleY      = ModSettings::g_ac_odyssey_ui_settings.hudScaleY;
        ui_vp->left       = (totalWidth - (totalWidth * scaleX)) / 2.f;
        ui_vp->right      = totalWidth * scaleX;
        ui_vp->top        = (totalHeight - (totalHeight * scaleY)) / 2.f;
        ui_vp->bottom     = totalHeight * scaleY;
    }
}
