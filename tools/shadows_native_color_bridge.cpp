#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#define ANVIL_SHADOWS_NATIVE_COLOR_BUILD
#include "anvilvr/ShadowsNativeColorBridge.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

using Microsoft::WRL::ComPtr;
namespace snc = anvilvr::shadows_native_color;

namespace {

constexpr wchar_t kDefaultMappingName[] = L"Local\\AnvilShadowsVR.NativeColor.v1";
thread_local HRESULT gLastConsumerImportHresult = S_OK;
thread_local HRESULT gLastPublisherCreateHresult = S_OK;

bool supportedFormat(DXGI_FORMAT format) noexcept {
    return format == DXGI_FORMAT_R8G8B8A8_UNORM ||
           format == DXGI_FORMAT_B8G8R8A8_UNORM ||
           format == DXGI_FORMAT_R10G10B10A2_UNORM ||
           format == DXGI_FORMAT_R16G16B16A16_FLOAT;
}

bool sameDevice(IUnknown* a, IUnknown* b) noexcept {
    ComPtr<IUnknown> identityA;
    ComPtr<IUnknown> identityB;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&identityA))) &&
           SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&identityB))) && identityA.Get() == identityB.Get();
}

std::uint64_t handleValue(HANDLE handle) noexcept {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
}

HANDLE handleFromValue(std::uint64_t value) noexcept {
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
}

struct PublisherSlot final {
    ComPtr<ID3D11Texture2D> ownerTexture11;
    ComPtr<ID3D12Resource> texture;
    HANDLE sharedHandle = nullptr;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    std::uint64_t lastSequence = 0;
};

struct Publisher final {
    CRITICAL_SECTION lock{};
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D11Device> ownerDevice11;
    DXGI_SWAP_CHAIN_DESC1 desc{};
    std::array<PublisherSlot, snc::kSlotCount> slots{};
    ComPtr<ID3D12Fence> producerFence;
    std::array<ComPtr<ID3D12Fence>, snc::kEyeCount> acknowledgeFences{};
    HANDLE producerFenceHandle = nullptr;
    HANDLE acknowledgeFenceHandles[snc::kEyeCount]{};
    HANDLE mapping = nullptr;
    snc::SharedHeader* header = nullptr;
    std::uint64_t generation = 0;
    std::uint64_t sequence = 0;
    std::uint32_t nextSlot[snc::kEyeCount]{};

    Publisher() {
        InitializeCriticalSection(&lock);
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        generation = static_cast<std::uint64_t>(counter.QuadPart) ^
                     (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32);
        if (generation == 0) generation = 1;
    }
    bool drain() noexcept {
        if (sequence == 0 || !producerFence || producerFence->GetCompletedValue() >= sequence) return true;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) return false;
        const HRESULT hr = producerFence->SetEventOnCompletion(sequence, event);
        const DWORD wait = SUCCEEDED(hr) ? WaitForSingleObject(event, 5000) : WAIT_FAILED;
        CloseHandle(event);
        return wait == WAIT_OBJECT_0;
    }
    ~Publisher() {
        if (header) UnmapViewOfFile(header);
        if (mapping) CloseHandle(mapping);
        for (auto& slot : slots) if (slot.sharedHandle) CloseHandle(slot.sharedHandle);
        if (producerFenceHandle) CloseHandle(producerFenceHandle);
        for (HANDLE handle : acknowledgeFenceHandles) if (handle) CloseHandle(handle);
        DeleteCriticalSection(&lock);
    }
};

struct ConsumerSlot final {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<IDXGIKeyedMutex> keyedMutex;
    bool acquired = false;
};

struct Consumer final {
    HANDLE mapping = nullptr;
    const snc::SharedHeader* header = nullptr;
    HANDLE producerProcess = nullptr;
    ComPtr<ID3D11Device5> device;
    ComPtr<ID3D11DeviceContext4> context;
    ComPtr<ID3D11Fence> producerFence;
    std::array<ComPtr<ID3D11Fence>, snc::kEyeCount> acknowledgeFences{};
    std::array<ConsumerSlot, snc::kSlotCount> slots{};
    std::uint64_t generation = 0;

    ~Consumer() {
        for (auto& slot : slots) {
            if (slot.acquired && slot.keyedMutex) slot.keyedMutex->ReleaseSync(0);
        }
        if (producerProcess) CloseHandle(producerProcess);
        if (header) UnmapViewOfFile(header);
        if (mapping) CloseHandle(mapping);
    }
};

class ScopedCriticalSection final {
public:
    explicit ScopedCriticalSection(CRITICAL_SECTION& lock) : lock_(lock) { EnterCriticalSection(&lock_); }
    ~ScopedCriticalSection() { LeaveCriticalSection(&lock_); }
private:
    CRITICAL_SECTION& lock_;
};

void beginHeaderWrite(snc::SharedHeader* header) noexcept {
    header->revision += 1;
    MemoryBarrier();
}

void endHeaderWrite(snc::SharedHeader* header) noexcept {
    MemoryBarrier();
    header->revision += 1;
}

bool copyStableHeader(const snc::SharedHeader* source, snc::SharedHeader& destination) noexcept {
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        const auto before = *reinterpret_cast<const volatile std::uint64_t*>(&source->revision);
        if ((before & 1u) != 0) { YieldProcessor(); continue; }
        std::memcpy(&destination, source, sizeof(destination));
        MemoryBarrier();
        const auto after = *reinterpret_cast<const volatile std::uint64_t*>(&source->revision);
        if (before == after && (after & 1u) == 0) return true;
    }
    return false;
}

HRESULT duplicateProducerHandle(HANDLE process, std::uint64_t sourceValue, HANDLE& destination) noexcept {
    destination = nullptr;
    if (!process || sourceValue == 0) return E_INVALIDARG;
    return DuplicateHandle(process, handleFromValue(sourceValue), GetCurrentProcess(), &destination,
                           0, FALSE, DUPLICATE_SAME_ACCESS)
               ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

std::wstring makeObjectName(const wchar_t* mapping, const wchar_t* kind, std::uint64_t generation, unsigned index = 0) {
    wchar_t buffer[320]{};
    swprintf_s(buffer, L"%s.%s.%lu.%016llX.%u", mapping, kind, GetCurrentProcessId(),
               static_cast<unsigned long long>(generation), index);
    return buffer;
}

std::uint32_t createPublisher(IUnknown* swapUnknown, IUnknown* queueUnknown,
                              const wchar_t* requestedName, Publisher*& output) {
    gLastPublisherCreateHresult = S_OK;
    output = nullptr;
    if (!swapUnknown || !queueUnknown) return snc::kInvalidArgument;
    auto instance = std::make_unique<Publisher>();
    HRESULT hr = swapUnknown->QueryInterface(IID_PPV_ARGS(&instance->swapChain));
    if (FAILED(hr)) return snc::kQueryInterfaceFailed;
    hr = queueUnknown->QueryInterface(IID_PPV_ARGS(&instance->queue));
    if (FAILED(hr)) return snc::kQueryInterfaceFailed;

    D3D12_COMMAND_QUEUE_DESC queueDesc = instance->queue->GetDesc();
    if (queueDesc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return snc::kWrongQueueType;
    ComPtr<ID3D12Device> queueDevice;
    ComPtr<ID3D12Device> swapDevice;
    hr = instance->queue->GetDevice(IID_PPV_ARGS(&queueDevice));
    if (FAILED(hr)) return snc::kQueryInterfaceFailed;
    hr = instance->swapChain->GetDevice(IID_PPV_ARGS(&swapDevice));
    if (FAILED(hr)) return snc::kQueryInterfaceFailed;
    if (!sameDevice(queueDevice.Get(), swapDevice.Get()) ||
        queueDevice->GetAdapterLuid().HighPart != swapDevice->GetAdapterLuid().HighPart ||
        queueDevice->GetAdapterLuid().LowPart != swapDevice->GetAdapterLuid().LowPart)
        return snc::kDeviceMismatch;
    instance->device = queueDevice;

    hr = instance->swapChain->GetDesc1(&instance->desc);
    if (FAILED(hr)) return snc::kCreateFailed;
    if (!supportedFormat(instance->desc.Format)) return snc::kUnsupportedFormat;
    if (instance->desc.SampleDesc.Count != 1 || instance->desc.Width == 0 || instance->desc.Height == 0)
        return snc::kUnsupportedBackBuffer;

    hr = instance->device->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
                                       IID_PPV_ARGS(&instance->producerFence));
    if (FAILED(hr)) return snc::kCreateFailed;
    const wchar_t* mappingName = requestedName && requestedName[0] ? requestedName : kDefaultMappingName;
    auto producerFenceName = makeObjectName(mappingName, L"ProducerFence", instance->generation);
    hr = instance->device->CreateSharedHandle(instance->producerFence.Get(), nullptr, GENERIC_ALL,
                                               producerFenceName.c_str(), &instance->producerFenceHandle);
    if (FAILED(hr)) return snc::kCreateFailed;
    for (std::uint32_t eye = 0; eye < snc::kEyeCount; ++eye) {
        hr = instance->device->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
                                           IID_PPV_ARGS(&instance->acknowledgeFences[eye]));
        if (FAILED(hr)) { gLastPublisherCreateHresult = hr; return snc::kCreateFailed; }
        auto acknowledgeFenceName = makeObjectName(mappingName, L"AcknowledgeFence", instance->generation, eye);
        hr = instance->device->CreateSharedHandle(instance->acknowledgeFences[eye].Get(), nullptr,
                                                   GENERIC_ALL, acknowledgeFenceName.c_str(),
                                                   &instance->acknowledgeFenceHandles[eye]);
        if (FAILED(hr)) return snc::kCreateFailed;
    }

    ComPtr<IDXGIFactory4> ownerFactory;
    hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&ownerFactory));
    if (FAILED(hr)) { gLastPublisherCreateHresult = hr; return snc::kCreateFailed; }
    ComPtr<IDXGIAdapter1> ownerAdapter;
    const LUID wantedLuid = instance->device->GetAdapterLuid();
    for (UINT index = 0; ; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        hr = ownerFactory->EnumAdapters1(index, &candidate);
        if (FAILED(hr)) break;
        DXGI_ADAPTER_DESC1 adapterDescription{};
        candidate->GetDesc1(&adapterDescription);
        if (adapterDescription.AdapterLuid.HighPart == wantedLuid.HighPart &&
            adapterDescription.AdapterLuid.LowPart == wantedLuid.LowPart) {
            ownerAdapter = candidate;
            break;
        }
    }
    if (!ownerAdapter) { gLastPublisherCreateHresult = DXGI_ERROR_NOT_FOUND; return snc::kCreateFailed; }
    D3D_FEATURE_LEVEL ownerFeatureLevel{};
    hr = D3D11CreateDevice(ownerAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                           &instance->ownerDevice11, &ownerFeatureLevel, nullptr);
    if (FAILED(hr)) { gLastPublisherCreateHresult = hr; return snc::kCreateFailed; }
    D3D11_TEXTURE2D_DESC texture11{};
    texture11.Width = instance->desc.Width;
    texture11.Height = instance->desc.Height;
    texture11.MipLevels = 1;
    texture11.ArraySize = 1;
    texture11.Format = instance->desc.Format;
    texture11.SampleDesc = {1, 0};
    texture11.Usage = D3D11_USAGE_DEFAULT;
    texture11.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texture11.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                          D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    for (std::uint32_t index = 0; index < snc::kSlotCount; ++index) {
        auto& slot = instance->slots[index];
        hr = instance->ownerDevice11->CreateTexture2D(&texture11, nullptr, &slot.ownerTexture11);
        if (FAILED(hr)) { gLastPublisherCreateHresult = hr; return snc::kCreateFailed; }
        auto textureName = makeObjectName(mappingName, L"Texture", instance->generation, index);
        ComPtr<IDXGIResource1> dxgiResource;
        hr = slot.ownerTexture11.As(&dxgiResource);
        if (SUCCEEDED(hr)) hr = dxgiResource->CreateSharedHandle(
            nullptr, GENERIC_ALL, textureName.c_str(), &slot.sharedHandle);
        if (SUCCEEDED(hr)) hr = instance->device->OpenSharedHandle(
            slot.sharedHandle, IID_PPV_ARGS(&slot.texture));
        if (FAILED(hr)) return snc::kCreateFailed;
        hr = instance->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       IID_PPV_ARGS(&slot.allocator));
        if (FAILED(hr)) return snc::kCreateFailed;
        hr = instance->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  slot.allocator.Get(), nullptr,
                                                  IID_PPV_ARGS(&slot.list));
        if (FAILED(hr) || FAILED(slot.list->Close())) return snc::kCreateFailed;
    }

    instance->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                            sizeof(snc::SharedHeader), mappingName);
    if (!instance->mapping) return snc::kCreateFailed;
    instance->header = static_cast<snc::SharedHeader*>(MapViewOfFile(
        instance->mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(snc::SharedHeader)));
    if (!instance->header) return snc::kCreateFailed;

    snc::SharedHeader initial{};
    initial.magic = snc::kMappingMagic;
    initial.version = snc::kAbiVersion;
    initial.size = sizeof(snc::SharedHeader);
    initial.generation = instance->generation;
    initial.producerPid = GetCurrentProcessId();
    initial.slotCount = snc::kSlotCount;
    initial.width = instance->desc.Width;
    initial.height = instance->desc.Height;
    initial.format = instance->desc.Format;
    initial.producerFenceHandle = handleValue(instance->producerFenceHandle);
    for (std::uint32_t eye = 0; eye < snc::kEyeCount; ++eye)
        initial.acknowledgeFenceHandle[eye] = handleValue(instance->acknowledgeFenceHandles[eye]);
    wcsncpy_s(initial.mappingName, mappingName, _TRUNCATE);
    for (std::uint32_t index = 0; index < snc::kSlotCount; ++index) {
        initial.slots[index].textureHandle = handleValue(instance->slots[index].sharedHandle);
        initial.slots[index].eye = index / snc::kSlotsPerEye;
        initial.slots[index].slot = index % snc::kSlotsPerEye;
    }
    // Publish the complete new generation without exposing a half-built header.
    auto* revision = reinterpret_cast<volatile LONG64*>(&instance->header->revision);
    const auto prior = InterlockedCompareExchange64(revision, 0, 0);
    InterlockedExchange64(revision, prior | 1);
    std::memcpy(instance->header, &initial, offsetof(snc::SharedHeader, revision));
    constexpr auto afterRevision = offsetof(snc::SharedHeader, revision) + sizeof(std::uint64_t);
    std::memcpy(reinterpret_cast<std::byte*>(instance->header) + afterRevision,
                reinterpret_cast<const std::byte*>(&initial) + afterRevision,
                sizeof(initial) - afterRevision);
    MemoryBarrier();
    InterlockedExchange64(revision, (prior | 1) + 1);
    output = instance.release();
    return snc::kOk;
}

std::uint32_t importConsumerResources(Consumer& consumer, const snc::SharedHeader& header) {
    gLastConsumerImportHresult = S_OK;
    if (header.magic != snc::kMappingMagic || header.version != snc::kAbiVersion ||
        header.size != sizeof(snc::SharedHeader) || header.slotCount != snc::kSlotCount)
        return snc::kMappingUnavailable;

    HANDLE process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, header.producerPid);
    if (!process) { gLastConsumerImportHresult = HRESULT_FROM_WIN32(GetLastError()); return snc::kMappingUnavailable; }
    HANDLE producerFenceHandle = nullptr;
    HRESULT hr = duplicateProducerHandle(process, header.producerFenceHandle, producerFenceHandle);
    if (FAILED(hr)) { gLastConsumerImportHresult = hr; CloseHandle(process); return snc::kCreateFailed; }
    ComPtr<ID3D11Fence> producerFence;
    hr = consumer.device->OpenSharedFence(producerFenceHandle, IID_PPV_ARGS(&producerFence));
    CloseHandle(producerFenceHandle);
    if (FAILED(hr)) { gLastConsumerImportHresult = hr; CloseHandle(process); return snc::kCreateFailed; }
    std::array<ComPtr<ID3D11Fence>, snc::kEyeCount> acknowledgeFences{};
    for (std::uint32_t eye = 0; eye < snc::kEyeCount; ++eye) {
        HANDLE acknowledgeFenceHandle = nullptr;
        hr = duplicateProducerHandle(process, header.acknowledgeFenceHandle[eye], acknowledgeFenceHandle);
        if (FAILED(hr)) { gLastConsumerImportHresult = hr; CloseHandle(process); return snc::kCreateFailed; }
        hr = consumer.device->OpenSharedFence(acknowledgeFenceHandle,
                                               IID_PPV_ARGS(&acknowledgeFences[eye]));
        CloseHandle(acknowledgeFenceHandle);
        if (FAILED(hr)) { gLastConsumerImportHresult = hr; CloseHandle(process); return snc::kCreateFailed; }
    }

    std::array<ConsumerSlot, snc::kSlotCount> slots{};
    for (std::uint32_t index = 0; index < snc::kSlotCount; ++index) {
        HANDLE textureHandle = nullptr;
        hr = duplicateProducerHandle(process, header.slots[index].textureHandle, textureHandle);
        if (FAILED(hr)) { gLastConsumerImportHresult = hr; CloseHandle(process); return snc::kCreateFailed; }
        ComPtr<ID3D11Resource> resource;
        hr = consumer.device->OpenSharedResource1(textureHandle, IID_PPV_ARGS(&resource));
        CloseHandle(textureHandle);
        if (FAILED(hr)) { gLastConsumerImportHresult = hr; CloseHandle(process); return snc::kCreateFailed; }
        hr = resource.As(&slots[index].texture);
        if (FAILED(hr)) { gLastConsumerImportHresult = hr; CloseHandle(process); return snc::kCreateFailed; }
        hr = resource.As(&slots[index].keyedMutex);
        if (FAILED(hr)) { gLastConsumerImportHresult = hr; CloseHandle(process); return snc::kCreateFailed; }
    }
    if (consumer.producerProcess) CloseHandle(consumer.producerProcess);
    consumer.producerProcess = process;
    consumer.producerFence = producerFence;
    consumer.acknowledgeFences = std::move(acknowledgeFences);
    consumer.slots = std::move(slots);
    consumer.generation = header.generation;
    return snc::kOk;
}

struct DummyObjects final {
    HWND window = nullptr;
    HINSTANCE windowInstance = nullptr;
    bool ownsWindowClass = false;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12CommandQueue> queue;
    ~DummyObjects() {
        swapChain.Reset();
        queue.Reset();
        if (window) DestroyWindow(window);
        if (ownsWindowClass && windowInstance)
            UnregisterClassW(L"AnvilShadowsNativeColorDummyWindowV2", windowInstance);
    }
};

HRESULT createDummyObjects(DummyObjects& objects, bool useWarp = true) {
    static const wchar_t windowClass[] = L"AnvilShadowsNativeColorDummyWindowV2";
    WNDCLASSW wc{};
    // Keep the registered callback inside user32, not this unloadable helper DLL. This also
    // makes an accidentally retained class harmless across a prototype DLL unload/reload.
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = windowClass;
    objects.windowInstance = wc.hInstance;
    objects.ownsWindowClass = RegisterClassW(&wc) != 0;
    if (!objects.ownsWindowClass && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    objects.window = CreateWindowExW(0, windowClass, L"", WS_POPUP, 0, 0, 16, 16,
                                     nullptr, nullptr, wc.hInstance, nullptr);
    if (!objects.window) return HRESULT_FROM_WIN32(GetLastError());
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;
    if (useWarp) {
        hr = factory->EnumWarpAdapter(IID_PPV_ARGS(&objects.adapter));
        if (FAILED(hr)) return hr;
    } else {
        for (UINT index = 0; ; ++index) {
            ComPtr<IDXGIAdapter1> candidate;
            hr = factory->EnumAdapters1(index, &candidate);
            if (hr == DXGI_ERROR_NOT_FOUND) return hr;
            if (FAILED(hr)) return hr;
            DXGI_ADAPTER_DESC1 adapterDesc{};
            candidate->GetDesc1(&adapterDesc);
            if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue;
            if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                                             __uuidof(ID3D12Device), nullptr))) {
                objects.adapter = candidate;
                break;
            }
        }
    }
    ComPtr<ID3D12Device> device;
    hr = D3D12CreateDevice(objects.adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) return hr;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&objects.queue));
    if (FAILED(hr)) return hr;
    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = 16;
    swapDesc.Height = 16;
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swap;
    hr = factory->CreateSwapChainForHwnd(objects.queue.Get(), objects.window, &swapDesc,
                                         nullptr, nullptr, &swap);
    if (FAILED(hr)) return hr;
    hr = swap.As(&objects.swapChain);
    return hr;
}

} // namespace

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_CreatePublisher(
    IUnknown* swapChain, IUnknown* directQueue, const wchar_t* mappingName, void** publisher) {
    if (!publisher) return snc::kInvalidArgument;
    *publisher = nullptr;
    Publisher* created = nullptr;
    const auto status = createPublisher(swapChain, directQueue, mappingName, created);
    *publisher = created;
    return status;
}

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_PublishEye(
    void* opaque, const snc::EyeFrameMetadata* metadata, snc::PublishResult* result) {
    if (!result) return snc::kInvalidArgument;
    *result = {};
    result->size = sizeof(*result);
    result->version = snc::kAbiVersion;
    if (!opaque || !metadata || metadata->size != sizeof(*metadata) ||
        metadata->version != snc::kAbiVersion || metadata->eye >= snc::kEyeCount) {
        result->status = snc::kInvalidArgument;
        result->hresult = E_INVALIDARG;
        return result->status;
    }
    auto& publisher = *static_cast<Publisher*>(opaque);
    ScopedCriticalSection guard(publisher.lock);
    DXGI_SWAP_CHAIN_DESC1 current{};
    HRESULT hr = publisher.swapChain->GetDesc1(&current);
    if (FAILED(hr) || current.Width != publisher.desc.Width || current.Height != publisher.desc.Height ||
        current.Format != publisher.desc.Format || current.SampleDesc.Count != 1) {
        result->status = snc::kDescriptionChanged;
        result->hresult = hr;
        return result->status;
    }
    const auto eye = metadata->eye;
    const std::uint64_t acknowledged = publisher.acknowledgeFences[eye]->GetCompletedValue();
    PublisherSlot* selected = nullptr;
    std::uint32_t selectedLocal = 0;
    for (std::uint32_t offset = 0; offset < snc::kSlotsPerEye; ++offset) {
        const auto local = (publisher.nextSlot[eye] + offset) % snc::kSlotsPerEye;
        auto& candidate = publisher.slots[eye * snc::kSlotsPerEye + local];
        if (candidate.lastSequence == 0 || acknowledged >= candidate.lastSequence) {
            selected = &candidate;
            selectedLocal = local;
            break;
        }
    }
    if (!selected) {
        result->status = snc::kNoFreeSlotDropped;
        result->hresult = DXGI_ERROR_WAS_STILL_DRAWING;
        return result->status;
    }
    ComPtr<ID3D12Resource> source;
    hr = publisher.swapChain->GetBuffer(publisher.swapChain->GetCurrentBackBufferIndex(),
                                         IID_PPV_ARGS(&source));
    if (FAILED(hr)) {
        result->status = snc::kExecuteFailed; result->hresult = hr; return result->status;
    }
    ComPtr<ID3D12Device> sourceDevice;
    hr = source->GetDevice(IID_PPV_ARGS(&sourceDevice));
    if (FAILED(hr) || !sameDevice(sourceDevice.Get(), publisher.device.Get())) {
        result->status = snc::kDeviceMismatch; result->hresult = hr; return result->status;
    }
    const auto sourceDesc = source->GetDesc();
    if (sourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || sourceDesc.SampleDesc.Count != 1 ||
        sourceDesc.Width != publisher.desc.Width || sourceDesc.Height != publisher.desc.Height ||
        sourceDesc.Format != publisher.desc.Format || !supportedFormat(sourceDesc.Format)) {
        result->status = snc::kUnsupportedBackBuffer; result->hresult = E_NOTIMPL; return result->status;
    }
    hr = selected->allocator->Reset();
    if (SUCCEEDED(hr)) hr = selected->list->Reset(selected->allocator.Get(), nullptr);
    if (FAILED(hr)) { result->status = snc::kExecuteFailed; result->hresult = hr; return result->status; }
    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition = {source.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                              D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE};
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition = {selected->texture.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                              D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST};
    selected->list->ResourceBarrier(2, barriers);
    selected->list->CopyResource(selected->texture.Get(), source.Get());
    std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
    std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
    selected->list->ResourceBarrier(2, barriers);
    hr = selected->list->Close();
    if (FAILED(hr)) { result->status = snc::kExecuteFailed; result->hresult = hr; return result->status; }
    ID3D12CommandList* lists[] = {selected->list.Get()};
    publisher.queue->ExecuteCommandLists(1, lists);
    const auto sequence = ++publisher.sequence;
    hr = publisher.queue->Signal(publisher.producerFence.Get(), sequence);
    if (FAILED(hr)) { result->status = snc::kExecuteFailed; result->hresult = hr; return result->status; }

    selected->lastSequence = sequence;
    publisher.nextSlot[eye] = (selectedLocal + 1) % snc::kSlotsPerEye;
    const std::uint32_t absolute = eye * snc::kSlotsPerEye + selectedLocal;
    beginHeaderWrite(publisher.header);
    auto& sharedSlot = publisher.header->slots[absolute];
    sharedSlot.publicationSequence = sequence;
    sharedSlot.producerFenceValue = sequence;
    sharedSlot.metadata = *metadata;
    publisher.header->latestSequence[eye] = sequence;
    publisher.header->latestSlot[eye] = absolute;
    endHeaderWrite(publisher.header);

    result->status = snc::kOk;
    result->hresult = S_OK;
    result->publicationSequence = sequence;
    result->producerFenceValue = sequence;
    result->eye = eye;
    result->slot = selectedLocal;
    return result->status;
}

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_PublisherIdle(void* opaque) {
    if (!opaque) return 1;
    auto& publisher = *static_cast<Publisher*>(opaque);
    ScopedCriticalSection guard(publisher.lock);
    return publisher.sequence == 0 || !publisher.producerFence ||
           publisher.producerFence->GetCompletedValue() >= publisher.sequence ? 1u : 0u;
}

ANVIL_SHADOWS_COLOR_API void AnvilShadowsNativeColor_DestroyPublisher(void* publisher) {
    auto* instance = static_cast<Publisher*>(publisher);
    if (!instance) return;
    // Never release allocators/resources while this DLL's copy work is still queued. If a
    // removed/hung device cannot drain, retain the small publisher allocation until process
    // shutdown rather than destabilizing the host process during prototype rollback.
    if (instance->drain()) delete instance;
}

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_CreateConsumer(
    const wchar_t* requestedName, IUnknown* deviceUnknown, IUnknown* contextUnknown, void** consumer) {
    if (!consumer) return snc::kInvalidArgument;
    *consumer = nullptr;
    if (!deviceUnknown || !contextUnknown) return snc::kInvalidArgument;
    auto instance = std::make_unique<Consumer>();
    HRESULT hr = deviceUnknown->QueryInterface(IID_PPV_ARGS(&instance->device));
    if (FAILED(hr)) return snc::kQueryInterfaceFailed;
    hr = contextUnknown->QueryInterface(IID_PPV_ARGS(&instance->context));
    if (FAILED(hr)) return snc::kQueryInterfaceFailed;
    const wchar_t* mappingName = requestedName && requestedName[0] ? requestedName : kDefaultMappingName;
    instance->mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mappingName);
    if (!instance->mapping) return snc::kMappingUnavailable;
    instance->header = static_cast<const snc::SharedHeader*>(MapViewOfFile(
        instance->mapping, FILE_MAP_READ, 0, 0, sizeof(snc::SharedHeader)));
    if (!instance->header) return snc::kMappingUnavailable;
    snc::SharedHeader snapshot{};
    if (!copyStableHeader(instance->header, snapshot)) return snc::kMappingUnavailable;
    const auto status = importConsumerResources(*instance, snapshot);
    if (status != snc::kOk) return status;
    *consumer = instance.release();
    return snc::kOk;
}

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_AcquireLatestEye(
    void* opaque, std::uint32_t eye, snc::ConsumerFrame* frame) {
    if (!frame) return snc::kInvalidArgument;
    *frame = {};
    frame->size = sizeof(*frame);
    frame->version = snc::kAbiVersion;
    if (!opaque || eye >= snc::kEyeCount) { frame->status = snc::kInvalidArgument; return frame->status; }
    auto& consumer = *static_cast<Consumer*>(opaque);
    snc::SharedHeader snapshot{};
    if (!copyStableHeader(consumer.header, snapshot)) { frame->status = snc::kMappingUnavailable; return frame->status; }
    if (snapshot.generation != consumer.generation) { frame->status = snc::kGenerationChanged; return frame->status; }
    const auto sequence = snapshot.latestSequence[eye];
    const auto index = snapshot.latestSlot[eye];
    if (sequence == 0 || index >= snc::kSlotCount || snapshot.slots[index].eye != eye ||
        snapshot.slots[index].publicationSequence != sequence) {
        frame->status = snc::kNoPublishedFrame; return frame->status;
    }
    auto& slot = consumer.slots[index];
    // These textures were created with SHARED_KEYEDMUTEX. A shared GPU fence
    // orders the copy but does not replace DXGI's resource-access contract.
    if (slot.acquired) { frame->status = snc::kNoPublishedFrame; return frame->status; }
    HRESULT hr = slot.keyedMutex->AcquireSync(0, 0);
    if (hr != S_OK) { frame->status = snc::kNoPublishedFrame; frame->hresult = hr; return frame->status; }
    slot.acquired = true;
    hr = consumer.context->Wait(consumer.producerFence.Get(),
                                         snapshot.slots[index].producerFenceValue);
    if (FAILED(hr)) {
        slot.keyedMutex->ReleaseSync(0);
        slot.acquired = false;
        frame->status = snc::kExecuteFailed; frame->hresult = hr; return frame->status;
    }
    frame->status = snc::kOk;
    frame->hresult = S_OK;
    frame->publicationSequence = sequence;
    frame->producerFenceValue = snapshot.slots[index].producerFenceValue;
    frame->generation = snapshot.generation;
    frame->width = snapshot.width;
    frame->height = snapshot.height;
    frame->format = snapshot.format;
    frame->eye = eye;
    frame->slot = index % snc::kSlotsPerEye;
    frame->metadata = snapshot.slots[index].metadata;
    frame->texture = consumer.slots[index].texture.Get();
    frame->texture->AddRef();
    return frame->status;
}

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_ReleaseEye(
    void* opaque, snc::ConsumerFrame* frame) {
    if (!opaque || !frame || frame->size != sizeof(*frame) || frame->version != snc::kAbiVersion ||
        !frame->texture || frame->publicationSequence == 0 || frame->eye >= snc::kEyeCount ||
        frame->slot >= snc::kSlotsPerEye)
        return snc::kInvalidArgument;
    auto& consumer = *static_cast<Consumer*>(opaque);
    const HRESULT hr = consumer.context->Signal(consumer.acknowledgeFences[frame->eye].Get(),
                                                  frame->publicationSequence);
    auto& slot = consumer.slots[frame->eye * snc::kSlotsPerEye + frame->slot];
    const HRESULT releaseHr = slot.acquired ? slot.keyedMutex->ReleaseSync(0) : E_UNEXPECTED;
    slot.acquired = false;
    frame->texture->Release();
    frame->texture = nullptr;
    frame->status = SUCCEEDED(hr) && SUCCEEDED(releaseHr) ? snc::kOk : snc::kExecuteFailed;
    frame->hresult = FAILED(hr) ? hr : releaseHr;
    return frame->status;
}

ANVIL_SHADOWS_COLOR_API void AnvilShadowsNativeColor_DestroyConsumer(void* consumer) {
    delete static_cast<Consumer*>(consumer);
}

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_GetHookAddresses(
    snc::HookAddresses* addresses) {
    if (!addresses) return snc::kInvalidArgument;
    *addresses = {};
    addresses->size = sizeof(*addresses);
    addresses->version = snc::kAbiVersion;
    DummyObjects objects;
    const HRESULT hr = createDummyObjects(objects);
    addresses->hresult = hr;
    if (FAILED(hr)) { addresses->status = snc::kCreateFailed; return addresses->status; }
    // Indices are the methods' positions in the SDK-declared COM interfaces. The objects
    // themselves are created through typed DXGI/D3D12 calls, so these are the active runtime's
    // code addresses rather than module/export guesses.
    auto** swapVtable = *reinterpret_cast<void***>(objects.swapChain.Get());
    auto** queueVtable = *reinterpret_cast<void***>(objects.queue.Get());
    addresses->present = reinterpret_cast<std::uint64_t>(swapVtable[8]);
    addresses->executeCommandLists = reinterpret_cast<std::uint64_t>(queueVtable[10]);
    addresses->status = addresses->present && addresses->executeCommandLists ? snc::kOk : snc::kCreateFailed;
    return addresses->status;
}

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_RunWarpSelfTest(
    snc::PublishResult* result);

namespace {
std::uint32_t runGpuSelfTest(snc::PublishResult* result, bool useWarp) {
    if (!result) return snc::kInvalidArgument;
    *result = {};
    result->size = sizeof(*result);
    result->version = snc::kAbiVersion;
    DummyObjects objects;
    HRESULT hr = createDummyObjects(objects, useWarp);
    if (FAILED(hr)) { result->status = snc::kCreateFailed; result->hresult = hr; return result->status; }
    wchar_t mappingName[128]{};
    swprintf_s(mappingName, L"Local\\AnvilShadowsVR.NativeColor.SelfTest.%lu", GetCurrentProcessId());
    void* publisher = nullptr;
    auto status = AnvilShadowsNativeColor_CreatePublisher(objects.swapChain.Get(), objects.queue.Get(),
                                                           mappingName, &publisher);
    result->eye = 1;
    if (status != snc::kOk) {
        result->status = status; result->hresult = gLastPublisherCreateHresult; return status;
    }

    ComPtr<ID3D11Device> d3d11Device;
    ComPtr<ID3D11DeviceContext> d3d11Context;
    D3D_FEATURE_LEVEL featureLevel{};
    hr = D3D11CreateDevice(objects.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                           &d3d11Device, &featureLevel, &d3d11Context);
    result->eye = 2;
    if (FAILED(hr)) {
        AnvilShadowsNativeColor_DestroyPublisher(publisher);
        result->status = snc::kCreateFailed; result->hresult = hr; return result->status;
    }
    void* consumer = nullptr;
    status = AnvilShadowsNativeColor_CreateConsumer(mappingName, d3d11Device.Get(),
                                                     d3d11Context.Get(), &consumer);
    result->eye = 3;
    if (status != snc::kOk) {
        AnvilShadowsNativeColor_DestroyPublisher(publisher);
        result->status = status; result->hresult = gLastConsumerImportHresult; return status;
    }
    snc::EyeFrameMetadata metadata{};
    metadata.sourceFrame = 1;
    metadata.poseSequence = 1;
    metadata.eye = 0;
    metadata.orientation[3] = 1.0f;
    metadata.fov[0] = -0.7f; metadata.fov[1] = 0.7f;
    metadata.fov[2] = 0.7f; metadata.fov[3] = -0.7f;
    status = AnvilShadowsNativeColor_PublishEye(publisher, &metadata, result);
    if (status == snc::kOk) {
        snc::ConsumerFrame frame{};
        status = AnvilShadowsNativeColor_AcquireLatestEye(consumer, 0, &frame);
        if (status == snc::kOk) {
            ComPtr<ID3D11Texture2D> sharedTexture;
            hr = frame.texture->QueryInterface(IID_PPV_ARGS(&sharedTexture));
            if (SUCCEEDED(hr)) {
                D3D11_TEXTURE2D_DESC sharedDesc{};
                sharedTexture->GetDesc(&sharedDesc);
                D3D11_TEXTURE2D_DESC stagingDesc = sharedDesc;
                stagingDesc.BindFlags = 0;
                stagingDesc.MiscFlags = 0;
                stagingDesc.Usage = D3D11_USAGE_STAGING;
                stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                ComPtr<ID3D11Texture2D> staging;
                hr = d3d11Device->CreateTexture2D(&stagingDesc, nullptr, &staging);
                if (SUCCEEDED(hr)) {
                    d3d11Context->CopyResource(staging.Get(), sharedTexture.Get());
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    hr = d3d11Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
                    if (SUCCEEDED(hr)) d3d11Context->Unmap(staging.Get(), 0);
                }
            }
            const auto releaseStatus = AnvilShadowsNativeColor_ReleaseEye(consumer, &frame);
            if (status == snc::kOk && releaseStatus != snc::kOk) status = releaseStatus;
        }
    }
    AnvilShadowsNativeColor_DestroyConsumer(consumer);
    AnvilShadowsNativeColor_DestroyPublisher(publisher);
    if (FAILED(hr) && status == snc::kOk) status = snc::kExecuteFailed;
    result->status = status;
    result->hresult = FAILED(hr) ? hr : S_OK;
    return status;
}
} // namespace

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_RunWarpSelfTest(
    snc::PublishResult* result) {
    return runGpuSelfTest(result, true);
}

ANVIL_SHADOWS_COLOR_API std::uint32_t AnvilShadowsNativeColor_RunGpuSelfTest(
    snc::PublishResult* result) {
    return runGpuSelfTest(result, false);
}

ANVIL_SHADOWS_COLOR_API const char* AnvilShadowsNativeColor_Schema() {
    return R"json({"schema":"anvilvr.shadows-native-color.v1","abiVersion":1,"eyes":2,"slotsPerEye":3,"formats":[10,24,28,87],"sourceState":"PRESENT","sharedState":"COMMON","busyPolicy":"drop","consumerSynchronization":"D3D11 context Wait/Signal per-eye acknowledge fences","sizes":{"EyeFrameMetadata":88,"PublishResult":40,"HookAddresses":32,"ConsumerFrame":160,"SharedSlot":120,"SharedHeader":1072},"EyeFrameMetadata":{"size":0,"version":4,"sourceFrame":8,"poseSequence":16,"predictedDisplayTime":24,"eye":32,"reserved":36,"orientation":40,"position":56,"fov":68},"PublishResult":{"size":0,"version":4,"status":8,"hresult":12,"publicationSequence":16,"producerFenceValue":24,"eye":32,"slot":36},"HookAddresses":{"size":0,"version":4,"present":8,"executeCommandLists":16,"hresult":24,"status":28},"ConsumerFrame":{"size":0,"version":4,"status":8,"hresult":12,"publicationSequence":16,"producerFenceValue":24,"generation":32,"width":40,"height":44,"format":48,"eye":52,"slot":56,"metadata":64,"texture":152},"SharedHeader":{"revision":8,"generation":16,"producerPid":24,"width":32,"height":36,"format":40,"producerFenceHandle":48,"acknowledgeFenceHandle":56,"latestSequence":72,"latestSlot":88,"mappingName":96,"slots":352},"SharedSlot":{"textureHandle":0,"publicationSequence":8,"producerFenceValue":16,"eye":24,"slot":28,"metadata":32}})json";
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
