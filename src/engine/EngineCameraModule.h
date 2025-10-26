#pragma once

#include <glm/detail/type_mat4x4.hpp>
#include <glm/detail/type_quat.hpp>
#include <glm/fwd.hpp>
#include <memory/FunctionHook.h>
#include <safetyhook/inline_hook.hpp>

namespace sdk
{
    struct Camera;
    struct CameraNode;
    struct ProjectionParameters;
} // namespace sdk

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

    safetyhook::InlineHook m_onCalcProjectionHook{};
    safetyhook::InlineHook m_onCalcMvpHook{};
    safetyhook::InlineHook m_onCameraGetForwardHook{};
    safetyhook::InlineHook m_onCalcUIViewportHook{};

public:
    static uintptr_t onCalcProjection(glm::mat4* pOutMatrix, float nearPlane, float farPlane, float fovy, float aspectRatio, float* pInputVector);
    static uintptr_t onCalcMvp(sdk::Camera* camera, uintptr_t a2, float* pNormalizedViewportRect, float* pOutRenderDimensions, float* pOutAbsoluteViewport,
                               float* pOutScaledViewport, float* pOutViewportScale, float* viewport, glm::mat4* pOutViewMatrix, glm::mat4* pOutViewProjectionMatrix,
                               glm::mat4* pOutProjectionMatrix, glm::mat4* pOutInverseViewMatrix, glm::mat4* pOutInverseProjectionMatrix, glm::mat4* pOutInvViewProjMatrix,
                               float* fovY, float* pAspectRatio, float nearPlane, float farPlane, uintptr_t pFrustum, uintptr_t a20);
    static glm::vec4* onCameraGetForwardHook(sdk::CameraNode* node, glm::vec4 *forward);
    static void onCalcUIViewportHook();


};
