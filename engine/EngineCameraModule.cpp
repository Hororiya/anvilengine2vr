#include "EngineCameraModule.h"
#include "EngineEntry.h"
#include "engine/memory/offsets.h"
#include "sdk/ACVRGfxContext.h"
#include <ModConfig.h>
#include <ModSettings.h>
#include <mods/VR.hpp>
#include <safetyhook/easy.hpp>
#include <utility/ScopeProfiler.h>

void EngineCameraModule::InstallHooks()
{
    spdlog::info("Installing EngineCameraModule hooks");
    //
    auto onCalcProjectionFN = memory::calc_projection_fn_addr();
    m_onCalcProjection      = safetyhook::create_inline((void*)onCalcProjectionFN, (void*)&EngineCameraModule::onCalcProjection);
    if (!m_onCalcProjection) {
        spdlog::error("Failed to hook m_onCalcProjection");
    }

    auto calcFinalViewFn = memory::update_views_fn_addr();
    m_onCalcFinalView    = safetyhook::create_inline((void*)calcFinalViewFn, (void*)&EngineCameraModule::onCalcFinalView);
    if (!m_onCalcFinalView) {
        spdlog::error("Failed to hook m_onCalcFinalView");
    }
    auto onCopyGfxContextFn = memory::on_gfx_context_copy();
    m_onCopyGfxContext      = safetyhook::create_inline((void*)onCopyGfxContextFn, (void*)&EngineCameraModule::onCopyGfxContext);
    if (!m_onCopyGfxContext) {
        spdlog::error("Failed to hook m_onCopyGfxContext");
    }

    //    auto onCameraGetRotationQuatHookFn = memory::on_camera_get_rotation_quat_addr();
    //    m_onCameraGetRotationQuatHook      = safetyhook::create_inline((void*)onCameraGetRotationQuatHookFn, (void*)&EngineCameraModule::onCameraGetRotationQuatHook);
    //    if (!m_onCameraGetRotationQuatHook) {
    //        spdlog::error("Failed to hook m_onCameraGetRotationQuatHook");
    //    }

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

thread_local bool inside_calc_mvp = false;

void EngineCameraModule::onCalcProjection(glm::mat4* outProjMatrix, float nearPlane, float farPlane, float fov, float aspectRatio, sdk::Viewport* viewport)
{
    SCOPE_PROFILER();
    static auto instance     = Get();
    static auto vr           = VR::get();
    instance->m_onCalcProjection.call<void>(outProjMatrix, nearPlane, farPlane, fov, aspectRatio, viewport);
    if (vr->is_hmd_active() && (inside_calc_mvp || farPlane > 1201.f)) {
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
}

const glm::mat4 Y_UP_TO_Z_UP_BASIS(1.0f, 0.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 1.0f, 0.0f,
                                   0.0f, -1.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 0.0f, 1.0f);

const glm::mat4 INV_UP_TO_Z_UP_BASIS = glm::inverse(Y_UP_TO_Z_UP_BASIS);

void removePitchFromZUpMatrix(glm::mat4& transform) {
    // Remove y component and normalize so we have the facing direction
    const auto forward_dir = glm::normalize(Vector3f{ -transform[1].x, 0.0, -transform[1].y });

    auto result = Y_UP_TO_Z_UP_BASIS * glm::lookAtRH(Vector3f{}, Vector3f{ forward_dir }, Vector3f(0.0f, 1.0f, 0.0f)) * INV_UP_TO_Z_UP_BASIS;
    result[3] = transform[3];
    transform = result;
}

float roundUp(float val) {
    return (float)((int)(((int)val + 1) & 0xFFFFFFFE));
}

uintptr_t EngineCameraModule::onCalcFinalView(sdk::GfxContext* context, int* out_textureId, float* out_textureScaleBias, float* out_viewport, float* out_scissorRect,
                                              float* out_scissorRectInt, float* out_scissorScale, float* projectionParams, glm::mat4* in_viewMatrix,
                                              glm::mat4* out_viewProjectionMatrix, glm::mat4* out_projectionMatrix, glm::mat4* out_inverseViewMatrix,
                                              glm::mat4* out_inverseProjectionMatrix, glm::mat4* out_worldToViewMatrix, float InFov, float* out_AspectRatio, float nearPlane,
                                              float farPlane, glm::mat4* out_frustumPlanes, struct GlobalGfxConstants* pGlobalConstants)
{
    SCOPE_PROFILER();
    static auto instance     = Get();
    static auto vr           = VR::get();
    static auto g_is_showing_ui = (uint8_t*)memory::g_is_showing_ui();
    bool        bIsShowingUI = g_is_showing_ui && (*g_is_showing_ui != 0xFF);
    if (vr->is_hmd_active() && !bIsShowingUI) {
        inside_calc_mvp = true;
        const auto        eye           = vr->get_current_eye_transform();
        const auto hmd_transform = vr->get_transform(0);
        const auto rotation_offset = vr->get_transform_offset();
        const auto transform       = Y_UP_TO_Z_UP_BASIS * rotation_offset * hmd_transform * eye * INV_UP_TO_Z_UP_BASIS;
        if(ModSettings::g_ac_mirage_settings.decoupledPitch) {
            removePitchFromZUpMatrix(*in_viewMatrix);
        }
        *in_viewMatrix       = *in_viewMatrix * transform;
        static glm::mat4 copies[2]{ glm::mat4{ 1.0 }, glm::mat4{ 1.0 } };
        const auto             frame = vr->m_presenter_frame_count;
        copies[frame % 2]      = *in_viewMatrix;
        if (context->pOverrideViewMatrix) {
            context->pastViewMatrix = *in_viewMatrix;
        }
    }
    auto result = instance->m_onCalcFinalView.call<uintptr_t>(context, out_textureId, out_textureScaleBias, out_viewport, out_scissorRect, out_scissorRectInt, out_scissorScale,
                                                       projectionParams, in_viewMatrix, out_viewProjectionMatrix, out_projectionMatrix, out_inverseViewMatrix,
                                                       out_inverseProjectionMatrix, out_worldToViewMatrix, InFov, out_AspectRatio, nearPlane, farPlane, out_frustumPlanes,
                                                       pGlobalConstants);
    if (vr->is_hmd_active()) {
        float offX = out_scissorRect[0] * 2.0f;
        float offY = out_scissorRect[1] * 2.0f;
        out_scissorRect[2] += offX;
        out_scissorRect[3] += offY;
        out_scissorRect[0] = 0.0f;
        out_scissorRect[1] = 0.0f;
        out_scissorRectInt[0] = 0.0f;
        out_scissorRectInt[1] = 0.0f;
        out_scissorRectInt[2] = roundUp(out_scissorRect[2]);
        out_scissorRectInt[3] = roundUp(out_scissorRect[3]);

        out_viewport[2] = out_scissorRect[2];
        out_viewport[3] = out_scissorRect[3];
    }

    inside_calc_mvp = false;
    return result;
}

uintptr_t EngineCameraModule::onCopyGfxContext(sdk::GfxContext* context, glm::mat4* viewMatrix)
{
    SCOPE_PROFILER();
    static auto      instance = Get();
    static auto      vr       = VR::get();
    static glm::mat4 copies[2]{ glm::mat4{ 1.0 }, glm::mat4{ 1.0 } };
    auto             result = instance->m_onCopyGfxContext.call<uintptr_t>(context, viewMatrix);

    auto frame              = vr->m_presenter_frame_count;
    copies[frame % 2]       = context->pastViewMatrix;
    context->pastViewMatrix = copies[(frame - 1) % 2];
    return result;
}

// THIS ONE S WORKING
// uintptr_t EngineCameraModule::onCameraGetRotationQuatHook(uintptr_t g, glm::vec4* pos, glm::quat* rot, float* a4)
//{
//    static auto vr = VR::get();
//    static auto instance = Get();
//    auto result = instance->m_onCameraGetRotationQuatHook.call<uintptr_t>(g, pos, rot, a4);
//    if(vr->is_hmd_active()) {
//        static auto hmd_rotation = glm::normalize(glm::quat(vr->get_rotation(0)));
//        if (vr->get_current_render_eye() == VRRuntime::Eye::LEFT) {
//            hmd_rotation = glm::normalize(glm::quat(vr->get_rotation(0)));
//        }
//        *rot = (*rot) * glm::quat{hmd_rotation.w, hmd_rotation.x, -hmd_rotation.z, hmd_rotation.y};
//    }
//    return result;
//}

glm::vec4* EngineCameraModule::onCameraGetForwardHook(sdk::CameraNode* node, glm::vec4* forward)
{
    static auto vr       = VR::get();
    static auto instance = Get();
    auto        result   = instance->m_onCameraGetForwardHook.call<glm::vec4*>(node, forward);
    if (vr->is_hmd_active()) {
        auto hmd_rotation = vr->get_transform(0);
        auto rotation = Y_UP_TO_Z_UP_BASIS * vr->get_transform_offset() * hmd_rotation * INV_UP_TO_Z_UP_BASIS;
        auto original_rotation_quat = node->prevRotation;
        if(ModSettings::g_ac_mirage_settings.decoupledPitch) {
            auto original_rotation = glm::mat4_cast(glm::normalize(node->prevRotation));
            removePitchFromZUpMatrix(original_rotation);
            original_rotation_quat = glm::normalize(glm::quat_cast(original_rotation));
        }
        *forward = original_rotation_quat * glm::normalize(glm::quat_cast(rotation)) * glm::vec4{ 0.0, 1.0, 0.0, 0.0 };
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
        float scaleX      = ModSettings::g_ac_mirage_settings.hudScaleX;
        float scaleY      = ModSettings::g_ac_mirage_settings.hudScaleY;
        ui_vp->left       = (totalWidth - (totalWidth * scaleX)) / 2.f;
        ui_vp->right      = totalWidth * scaleX;
        ui_vp->top        = (totalHeight - (totalHeight * scaleY)) / 2.f;
        ui_vp->bottom     = totalHeight * scaleY;
    }
}
