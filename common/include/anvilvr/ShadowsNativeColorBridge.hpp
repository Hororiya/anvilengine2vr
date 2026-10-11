#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

struct IUnknown;

namespace anvilvr::shadows_native_color {

inline constexpr std::uint32_t kAbiVersion = 1;
inline constexpr std::uint32_t kMappingMagic = 0x434E5641u; // "AVNC"
inline constexpr std::uint32_t kEyeCount = 2;
inline constexpr std::uint32_t kSlotsPerEye = 3;
inline constexpr std::uint32_t kSlotCount = kEyeCount * kSlotsPerEye;
inline constexpr std::uint32_t kNameCapacity = 128;

enum Status : std::uint32_t {
    kOk = 0,
    kInvalidArgument = 1,
    kQueryInterfaceFailed = 2,
    kWrongQueueType = 3,
    kDeviceMismatch = 4,
    kUnsupportedFormat = 5,
    kUnsupportedBackBuffer = 6,
    kCreateFailed = 7,
    kNoFreeSlotDropped = 8,
    kExecuteFailed = 9,
    kDescriptionChanged = 10,
    kMappingUnavailable = 11,
    kNoPublishedFrame = 12,
    kGenerationChanged = 13,
    kStructuredException = 14,
};

struct EyeFrameMetadata final {
    std::uint32_t size = sizeof(EyeFrameMetadata);
    std::uint32_t version = kAbiVersion;
    std::uint64_t sourceFrame = 0;
    std::uint64_t poseSequence = 0;
    std::int64_t predictedDisplayTime = 0;
    std::uint32_t eye = 0;
    std::uint32_t reserved = 0; // Optional explicit AER pair ID; 0 = legacy producer.
    float orientation[4]{}; // x, y, z, w in the source OpenXR local space
    float position[3]{};    // metres
    float fov[4]{};         // left, right, up, down radians
};

struct PublishResult final {
    std::uint32_t size = sizeof(PublishResult);
    std::uint32_t version = kAbiVersion;
    std::uint32_t status = kInvalidArgument;
    std::int32_t hresult = 0;
    std::uint64_t publicationSequence = 0;
    std::uint64_t producerFenceValue = 0;
    std::uint32_t eye = 0;
    std::uint32_t slot = 0;
};

struct HookAddresses final {
    std::uint32_t size = sizeof(HookAddresses);
    std::uint32_t version = kAbiVersion;
    std::uint64_t present = 0;
    std::uint64_t executeCommandLists = 0;
    std::int32_t hresult = 0;
    std::uint32_t status = kInvalidArgument;
};

struct ConsumerFrame final {
    std::uint32_t size = sizeof(ConsumerFrame);
    std::uint32_t version = kAbiVersion;
    std::uint32_t status = kInvalidArgument;
    std::int32_t hresult = 0;
    std::uint64_t publicationSequence = 0;
    std::uint64_t producerFenceValue = 0;
    std::uint64_t generation = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t format = 0;
    std::uint32_t eye = 0;
    std::uint32_t slot = 0;
    std::uint32_t reserved = 0;
    EyeFrameMetadata metadata{};
    IUnknown* texture = nullptr; // ID3D11Texture2D; released by ConsumerRelease.
};

struct SharedSlot final {
    std::uint64_t textureHandle = 0; // Handle value in producerPid; duplicate before use.
    std::uint64_t publicationSequence = 0;
    std::uint64_t producerFenceValue = 0;
    std::uint32_t eye = 0;
    std::uint32_t slot = 0;
    EyeFrameMetadata metadata{};
};

struct SharedHeader final {
    std::uint32_t magic = kMappingMagic;
    std::uint16_t version = kAbiVersion;
    std::uint16_t size = sizeof(SharedHeader);
    std::uint64_t revision = 0; // even=stable, odd=writer is updating
    std::uint64_t generation = 0;
    std::uint32_t producerPid = 0;
    std::uint32_t slotCount = kSlotCount;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t format = 0;
    std::uint32_t reserved = 0;
    std::uint64_t producerFenceHandle = 0;
    std::uint64_t acknowledgeFenceHandle[kEyeCount]{};
    std::uint64_t latestSequence[kEyeCount]{};
    std::uint32_t latestSlot[kEyeCount]{};
    wchar_t mappingName[kNameCapacity]{};
    SharedSlot slots[kSlotCount]{};
};

static_assert(std::is_standard_layout_v<EyeFrameMetadata> &&
              std::is_trivially_copyable_v<EyeFrameMetadata>);
static_assert(sizeof(EyeFrameMetadata) == 88);
static_assert(offsetof(EyeFrameMetadata, sourceFrame) == 8);
static_assert(offsetof(EyeFrameMetadata, poseSequence) == 16);
static_assert(offsetof(EyeFrameMetadata, predictedDisplayTime) == 24);
static_assert(offsetof(EyeFrameMetadata, eye) == 32);
static_assert(offsetof(EyeFrameMetadata, orientation) == 40);
static_assert(offsetof(EyeFrameMetadata, position) == 56);
static_assert(offsetof(EyeFrameMetadata, fov) == 68);
static_assert(sizeof(PublishResult) == 40);
static_assert(offsetof(PublishResult, publicationSequence) == 16);
static_assert(offsetof(PublishResult, producerFenceValue) == 24);
static_assert(offsetof(PublishResult, eye) == 32);
static_assert(sizeof(HookAddresses) == 32);
static_assert(offsetof(HookAddresses, present) == 8);
static_assert(offsetof(HookAddresses, executeCommandLists) == 16);
static_assert(offsetof(HookAddresses, hresult) == 24);
static_assert(offsetof(HookAddresses, status) == 28);
static_assert(sizeof(SharedSlot) == 120);
static_assert(sizeof(SharedHeader) == 1072);
static_assert(sizeof(ConsumerFrame) == 160);
static_assert(offsetof(SharedHeader, revision) == 8);
static_assert(offsetof(SharedHeader, producerFenceHandle) == 48);
static_assert(offsetof(SharedHeader, acknowledgeFenceHandle) == 56);
static_assert(offsetof(SharedHeader, latestSequence) == 72);
static_assert(offsetof(SharedHeader, latestSlot) == 88);
static_assert(offsetof(SharedHeader, mappingName) == 96);
static_assert(offsetof(SharedHeader, slots) == 352);
static_assert(alignof(SharedHeader) == 8);

} // namespace anvilvr::shadows_native_color

#if defined(_WIN32)
#if defined(ANVIL_SHADOWS_NATIVE_COLOR_BUILD)
#define ANVIL_SHADOWS_COLOR_API extern "C" __declspec(dllexport)
#else
#define ANVIL_SHADOWS_COLOR_API extern "C" __declspec(dllimport)
#endif
#else
#define ANVIL_SHADOWS_COLOR_API extern "C"
#endif

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_CreatePublisher(
    IUnknown* swapChain, IUnknown* directQueue, const wchar_t* mappingName, void** publisher);
ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_PublishEye(
    void* publisher, const anvilvr::shadows_native_color::EyeFrameMetadata* metadata,
    anvilvr::shadows_native_color::PublishResult* result);
// Nonblocking gate before retiring a publisher from a Present callback.
ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_PublisherIdle(void* publisher);
ANVIL_SHADOWS_COLOR_API void AnvilShadowsNativeColor_DestroyPublisher(void* publisher);

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_CreateConsumer(
    const wchar_t* mappingName, IUnknown* d3d11Device, IUnknown* d3d11ImmediateContext,
    void** consumer);
ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_AcquireLatestEye(
    void* consumer, std::uint32_t eye,
    anvilvr::shadows_native_color::ConsumerFrame* frame);
ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_ReleaseEye(
    void* consumer, anvilvr::shadows_native_color::ConsumerFrame* frame);
ANVIL_SHADOWS_COLOR_API void AnvilShadowsNativeColor_DestroyConsumer(void* consumer);

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_GetHookAddresses(
    anvilvr::shadows_native_color::HookAddresses* addresses);
ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_RunWarpSelfTest(
    anvilvr::shadows_native_color::PublishResult* result);
ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_RunGpuSelfTest(
    anvilvr::shadows_native_color::PublishResult* result);
ANVIL_SHADOWS_COLOR_API const char* AnvilShadowsNativeColor_Schema();
