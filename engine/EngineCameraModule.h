#pragma once

#include <glm/detail/type_mat4x4.hpp>
#include <safetyhook/inline_hook.hpp>
#include <utils/FunctionHook.h>

namespace sdk
{

    struct Matrix4x4
    {
        glm::vec4 row[4];
    };

    struct Viewport
    {
        char    pad[0xAF];
        uint8_t orpho;
    };

    static_assert(offsetof(Viewport, orpho) == 0xAF, "Viewport::orpho offset is incorrect");

    struct CameraClient
    {
        void*          __vtbl;
        char           pad8[48];
        char*          name;
        char           pad40[18];
        uint16_t       flags;
        char           pad58[300];
        sdk::Viewport* pViewport;
        uint8_t        flag;
        char           pad190[23];
        float          perspectiveFov;
        float          orphoFov;
    };

    static_assert(offsetof(CameraClient, pViewport) == 0x180, "CameraObj::pViewport offset is incorrect");

    struct CameraBlender
    {
        void*     __vtbl;
        char      pad8[0xAA0 - 8];
        Matrix4x4 view;
        Matrix4x4 viewCopy;
    };

    static_assert(offsetof(CameraBlender, view) == 0xAA0, "CameraBlender::view offset is incorrect");

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

    safetyhook::InlineHook m_onCalcProjection{};
    safetyhook::InlineHook m_onCalcFinalView{};

public:
    static sdk::Matrix4x4* onCalcProjection(sdk::CameraClient* a1, float aspectRatio, uint8_t bUseCustomAspectRatio, float customAspectRatioFactor, float nearZ, float farZ,
                                            float viewportWidth, float viewportHeight, uint8_t bSomeFlag, sdk::Matrix4x4* outProjMatrix, sdk::Matrix4x4* outInvProjMatrix,
                                            float* outTanHalfFovX, float* outTanHalfFovY, sdk::Matrix4x4* outJitterMatrix);

    static void onCalcFinalView(sdk::CameraBlender* a1);
};
