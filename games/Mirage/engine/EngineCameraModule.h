#pragma once

#include <glm/detail/type_mat4x4.hpp>
#include <glm/detail/type_quat.hpp>
#include <glm/fwd.hpp>
#include <memory/FunctionHook.h>
#include <safetyhook/inline_hook.hpp>

namespace sdk {
    struct GfxContext;
    struct Viewport;
    struct CameraNode;
    struct GlobalGfxConstants;
}
class EngineCameraModule
{
public:
    void InstallHooks();

    inline static EngineCameraModule* Get()
    {
        static auto instance(new EngineCameraModule);
        return instance;
    }

private:
    EngineCameraModule()  = default;
    ~EngineCameraModule() = default;

    safetyhook::InlineHook m_onCalcProjection{};
    safetyhook::InlineHook m_onCalcFinalView{};
    safetyhook::InlineHook m_onCameraGetRotationQuatHook{};
    safetyhook::InlineHook m_onCameraGetAdditionalRotationQuatHook{};
    safetyhook::InlineHook m_onCameraUpdateCoreHook{};
    safetyhook::InlineHook m_onCameraGetForwardHook{};
    safetyhook::InlineHook m_onSetScissorsHook{};
    safetyhook::InlineHook m_onCalcUIViewportHook{};
    safetyhook::InlineHook m_onCopyGfxContext{};

public:
    static void onCalcProjection(glm::mat4 *out_projectionMatrix,
                                 float nearPlane,
                                 float farPlane,
                                 float fov,
                                 float aspectRatio,
                                 sdk::Viewport* viewport);

    static glm::vec4* onCameraGetForwardHook(sdk::CameraNode* node, glm::vec4 *forward);

    static uintptr_t onCopyGfxContext(sdk::GfxContext* context, glm::mat4* viewMatrix);

    static void onCalcUIViewportHook();

    static uintptr_t onCalcFinalView(sdk::GfxContext* context,
                                     int *out_textureId,
                                     float *out_textureScaleBias,
                                     float *out_viewport,
                                     float *out_scissorRect,
                                     float *out_scissorRectInt,
                                     float *out_scissorScale,
                                     float *projectionParams,
                                     glm::mat4 *in_viewMatrix,
                                     glm::mat4 *out_viewProjectionMatrix,
                                     glm::mat4 *out_projectionMatrix,
                                     glm::mat4 *out_inverseViewMatrix,
                                     glm::mat4 *out_inverseProjectionMatrix,
                                     glm::mat4 *out_worldToViewMatrix,
                                     float InFov,
                                     float *out_AspectRatio,
                                     float nearPlane,
                                     float farPlane,
                                     glm::mat4 *out_frustumPlanes,
                                     struct GlobalGfxConstants *pGlobalConstants);
};
