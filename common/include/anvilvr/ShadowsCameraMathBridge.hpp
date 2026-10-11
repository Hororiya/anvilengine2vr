#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(_WIN32) && defined(ANVIL_SHADOWS_CAMERA_BRIDGE_BUILD)
#define ANVIL_SHADOWS_CAMERA_BRIDGE_API extern "C" __declspec(dllexport)
#elif defined(_WIN32)
#define ANVIL_SHADOWS_CAMERA_BRIDGE_API extern "C" __declspec(dllimport)
#else
#define ANVIL_SHADOWS_CAMERA_BRIDGE_API extern "C"
#endif

#if defined(_MSC_VER)
#define ANVIL_SHADOWS_CAMERA_BRIDGE_CALL __cdecl
#else
#define ANVIL_SHADOWS_CAMERA_BRIDGE_CALL
#endif

// Fixed, language-neutral ABI used by the narrowly scoped game-camera hook.
// Matrices use column-major storage. anvilC2W290 is the measured Anvil layout:
// columns right, forward, up, positionHigh. originalPositionLow2D0 is the
// matching float4 residual. outputAnvilC2W/outputPositionLow preserve that
// high+low split rather than quantizing the VR eye translation to one float.
// outputXRProjectionColumnMajor is an OpenXR-style RH, -Z-forward projection.

enum AnvilShadowsCameraBridgeStatus : std::uint32_t {
    ANVIL_SHADOWS_CAMERA_BRIDGE_READY = 0,
    ANVIL_SHADOWS_CAMERA_BRIDGE_NULL_ARGUMENT = 1,
    ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_EYE = 2,
    ANVIL_SHADOWS_CAMERA_BRIDGE_NON_FINITE_INPUT = 3,
    ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_UNITS_PER_METRE = 4,
    ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_CLIP_PLANES = 5,
    ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_BASE_BASIS = 6,
    ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_POSE = 7,
    ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_FOV = 8,
    ANVIL_SHADOWS_CAMERA_BRIDGE_MAPPING_UNAVAILABLE = 9,
    ANVIL_SHADOWS_CAMERA_BRIDGE_POSE_READ_FAILED = 10,
    ANVIL_SHADOWS_CAMERA_BRIDGE_POSE_INVALID = 11,
    ANVIL_SHADOWS_CAMERA_BRIDGE_POSE_STALE = 12,
    ANVIL_SHADOWS_CAMERA_BRIDGE_INTERNAL_ERROR = 13,
};

struct AnvilShadowsCameraSyntheticPose final {
    std::uint64_t poseSequence;
    std::int64_t predictedDisplayTime;
    std::int64_t hostQpc;
    float recenterOrientation[4]; // x, y, z, w
    float recenterPosition[3];    // metres
    std::uint32_t reserved0;
    float absoluteEyeOrientation[4]; // x, y, z, w
    float absoluteEyePosition[3];    // metres
    std::uint32_t reserved1;
    float fov[4]; // angleLeft, angleRight, angleUp, angleDown, radians
};

struct AnvilShadowsCameraBridgeOutput final {
    std::uint32_t status;
    std::uint32_t eyeIndex;
    std::uint64_t poseSequence;
    std::int64_t predictedDisplayTime;
    std::int64_t hostQpc;
    float sourceAbsoluteEyeOrientation[4];
    float sourceAbsoluteEyePosition[3];
    std::uint32_t reserved0;
    float sourceFov[4];
    float outputAnvilC2W[16];
    float outputPositionLow[4];
    float outputXRProjectionColumnMajor[16];
    float outputRightUpBackFloat4[12];
};

static_assert(std::is_standard_layout_v<AnvilShadowsCameraSyntheticPose>);
static_assert(std::is_trivially_copyable_v<AnvilShadowsCameraSyntheticPose>);
static_assert(sizeof(AnvilShadowsCameraSyntheticPose) == 104);
static_assert(offsetof(AnvilShadowsCameraSyntheticPose, poseSequence) == 0);
static_assert(offsetof(AnvilShadowsCameraSyntheticPose, predictedDisplayTime) == 8);
static_assert(offsetof(AnvilShadowsCameraSyntheticPose, hostQpc) == 16);
static_assert(offsetof(AnvilShadowsCameraSyntheticPose, recenterOrientation) == 24);
static_assert(offsetof(AnvilShadowsCameraSyntheticPose, recenterPosition) == 40);
static_assert(offsetof(AnvilShadowsCameraSyntheticPose, absoluteEyeOrientation) == 56);
static_assert(offsetof(AnvilShadowsCameraSyntheticPose, absoluteEyePosition) == 72);
static_assert(offsetof(AnvilShadowsCameraSyntheticPose, fov) == 88);

static_assert(std::is_standard_layout_v<AnvilShadowsCameraBridgeOutput>);
static_assert(std::is_trivially_copyable_v<AnvilShadowsCameraBridgeOutput>);
static_assert(sizeof(AnvilShadowsCameraBridgeOutput) == 272);
static_assert(offsetof(AnvilShadowsCameraBridgeOutput, status) == 0);
static_assert(offsetof(AnvilShadowsCameraBridgeOutput, eyeIndex) == 4);
static_assert(offsetof(AnvilShadowsCameraBridgeOutput, poseSequence) == 8);
static_assert(offsetof(AnvilShadowsCameraBridgeOutput, predictedDisplayTime) == 16);
static_assert(offsetof(AnvilShadowsCameraBridgeOutput, hostQpc) == 24);
static_assert(offsetof(AnvilShadowsCameraBridgeOutput, sourceAbsoluteEyeOrientation) == 32);
static_assert(offsetof(AnvilShadowsCameraBridgeOutput, sourceAbsoluteEyePosition) == 48);
static_assert(offsetof(AnvilShadowsCameraBridgeOutput, sourceFov) == 64);
static_assert(offsetof(AnvilShadowsCameraBridgeOutput, outputAnvilC2W) == 80);
static_assert(offsetof(AnvilShadowsCameraBridgeOutput, outputPositionLow) == 144);
static_assert(offsetof(AnvilShadowsCameraBridgeOutput, outputXRProjectionColumnMajor) == 160);
static_assert(offsetof(AnvilShadowsCameraBridgeOutput, outputRightUpBackFloat4) == 224);

// Argument order is deliberately identical up to eyeIndex for the pure and live
// paths, making the Frida NativeFunction declarations difficult to mix up.
ANVIL_SHADOWS_CAMERA_BRIDGE_API std::uint32_t ANVIL_SHADOWS_CAMERA_BRIDGE_CALL
AnvilShadowsCameraBridgeComputeSynthetic(
    const float* anvilC2W290,
    const float* originalPositionLow2D0,
    double unitsPerMetre,
    double nearPlane,
    double farPlane,
    std::uint32_t eyeIndex,
    const AnvilShadowsCameraSyntheticPose* syntheticPose,
    AnvilShadowsCameraBridgeOutput* output) noexcept;

ANVIL_SHADOWS_CAMERA_BRIDGE_API std::uint32_t ANVIL_SHADOWS_CAMERA_BRIDGE_CALL
AnvilShadowsCameraBridgeComputeLive(
    const float* anvilC2W290,
    const float* originalPositionLow2D0,
    double unitsPerMetre,
    double nearPlane,
    double farPlane,
    std::uint32_t eyeIndex,
    AnvilShadowsCameraBridgeOutput* output) noexcept;

// AER pair: eye 0 latches both XR poses; eye 1 consumes the same <=100ms snapshot.
// Call order must be 0 then 1. A failed pair must restart with eye 0.
ANVIL_SHADOWS_CAMERA_BRIDGE_API std::uint32_t ANVIL_SHADOWS_CAMERA_BRIDGE_CALL
AnvilShadowsCameraBridgeComputePairedLive(
    const float* anvilC2W290, const float* originalPositionLow2D0,
    double unitsPerMetre, double nearPlane, double farPlane,
    std::uint32_t eyeIndex, AnvilShadowsCameraBridgeOutput* output) noexcept;

// Reset makes the next valid live sample the new center. Recenter captures a
// valid, <=100 ms old sample immediately. Shutdown releases mapping handles.
ANVIL_SHADOWS_CAMERA_BRIDGE_API std::uint32_t ANVIL_SHADOWS_CAMERA_BRIDGE_CALL
AnvilShadowsCameraBridgeReset() noexcept;

ANVIL_SHADOWS_CAMERA_BRIDGE_API std::uint32_t ANVIL_SHADOWS_CAMERA_BRIDGE_CALL
AnvilShadowsCameraBridgeRecenter() noexcept;

ANVIL_SHADOWS_CAMERA_BRIDGE_API void ANVIL_SHADOWS_CAMERA_BRIDGE_CALL
AnvilShadowsCameraBridgeShutdown() noexcept;
