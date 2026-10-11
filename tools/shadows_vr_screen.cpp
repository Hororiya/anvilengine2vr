#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11

#include <windows.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <d3d11.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <anvilvr/ShadowsPoseTransport.hpp>
#include <anvilvr/ShadowsNativeColorBridge.hpp>
#include "shadows_eye_gaze.hpp"
#include <anvilvr/ShadowsStereoPairMatcher.hpp>
#include <anvilvr/ShadowsConsumerRecoveryPolicy.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

// Baseline transport only: this captures the existing flat game window and
// projects it onto a world-locked plane rendered separately for both OpenXR
// eyes. It does not inject into the game, synthesize native stereo, or replace
// the game's keyboard/controller input path. Desktop Duplication requires the
// target window to remain visible, non-minimized, and on an output driven by
// the same adapter selected by the OpenXR runtime.

struct Options {
    std::wstring process = L"ACShadows.exe";
    std::filesystem::path logPath;
    std::filesystem::path screenshotPath;
    double durationSeconds = 60.0;
    double waitWindowSeconds = 120.0;
    float widthMetres = 4.0f;
    float distanceMetres = 3.0f;
    bool probeOnly = false;
    bool headLocked = false;
    bool restoreWindow = false;
    bool controlSelfTest = false;
    bool nativeStereo = false;
    std::filesystem::path nativeColorHelper =
        std::filesystem::path(L"build") / L"full" / L"Release" / L"shadows_native_color_bridge.dll";
    std::wstring nativeColorMapping = L"Local\\AnvilShadowsVR.NativeColor.v1";
};

class Logger {
public:
    explicit Logger(const std::filesystem::path& path) {
        if (!path.empty()) {
            file_.open(path, std::ios::out | std::ios::trunc);
            if (!file_) throw std::runtime_error("failed to open log file");
        }
    }

    template <typename... T>
    void line(T&&... values) {
        std::ostringstream s;
        (s << ... << values);
        std::cout << s.str() << '\n';
        if (file_) {
            file_ << s.str() << '\n';
            file_.flush();
        }
    }

private:
    std::ofstream file_;
};

class StopControl {
public:
    StopControl() {
        name_ = L"Local\\AnvilShadowsVR.Stop." + std::to_wstring(GetCurrentProcessId());
        event_ = CreateEventW(nullptr, TRUE, FALSE, name_.c_str());
        if (!event_) throw std::runtime_error("CreateEventW(stop control) failed");
    }
    ~StopControl() { if (event_) CloseHandle(event_); }
    StopControl(const StopControl&) = delete;
    StopControl& operator=(const StopControl&) = delete;

    HANDLE handle() const { return event_; }
    const std::wstring& name() const { return name_; }
    bool requested() const { return WaitForSingleObject(event_, 0) == WAIT_OBJECT_0; }

    bool selfTest(Logger& log) {
        HANDLE opened = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, name_.c_str());
        if (!opened) throw std::runtime_error("OpenEventW(control self-test) failed");
        const BOOL setOk = SetEvent(opened);
        const bool observedSet = WaitForSingleObject(event_, 0) == WAIT_OBJECT_0;
        const BOOL resetOk = ResetEvent(opened);
        const bool observedReset = WaitForSingleObject(event_, 0) == WAIT_TIMEOUT;
        CloseHandle(opened);
        log.line("control_self_test create=true open=true set=", setOk ? "true" : "false",
                 " observed_set=", observedSet ? "true" : "false",
                 " reset=", resetOk ? "true" : "false",
                 " observed_reset=", observedReset ? "true" : "false");
        return setOk && observedSet && resetOk && observedReset;
    }

private:
    HANDLE event_ = nullptr;
    std::wstring name_;
};

std::string hexHr(HRESULT hr) {
    std::ostringstream s;
    s << "0x" << std::hex << std::uppercase << static_cast<uint32_t>(hr);
    return s.str();
}

void checkHr(HRESULT hr, const char* operation) {
    if (FAILED(hr)) throw std::runtime_error(std::string(operation) + " failed: " + hexHr(hr));
}

void checkXr(XrResult result, const char* operation) {
    if (XR_FAILED(result)) {
        throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
    }
}

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

std::wstring basename(std::wstring value) {
    const auto pos = value.find_last_of(L"\\/");
    return pos == std::wstring::npos ? value : value.substr(pos + 1);
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "<unprintable>";
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

Options parseOptions(int argc, wchar_t** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        const auto requireValue = [&]() -> std::wstring {
            if (++i >= argc) throw std::runtime_error("missing value after command-line option");
            return argv[i];
        };
        if (arg == L"--process") options.process = requireValue();
        else if (arg == L"--duration") options.durationSeconds = std::stod(requireValue());
        else if (arg == L"--wait-window") options.waitWindowSeconds = std::stod(requireValue());
        else if (arg == L"--log") options.logPath = requireValue();
        else if (arg == L"--screenshot") options.screenshotPath = requireValue();
        else if (arg == L"--width") options.widthMetres = std::stof(requireValue());
        else if (arg == L"--distance") options.distanceMetres = std::stof(requireValue());
        else if (arg == L"--head-locked") options.headLocked = true;
        else if (arg == L"--restore-window") options.restoreWindow = true;
        else if (arg == L"--control-self-test") options.controlSelfTest = true;
        else if (arg == L"--native-stereo") options.nativeStereo = true;
        else if (arg == L"--native-color-helper") options.nativeColorHelper = requireValue();
        else if (arg == L"--native-color-mapping") options.nativeColorMapping = requireValue();
        else if (arg == L"--probe-only") options.probeOnly = true;
        else if (arg == L"--help" || arg == L"-h") {
            std::wcout << L"shadows_vr_screen [--process ACShadows.exe] [--duration 60] "
                          L"[--wait-window 120] [--restore-window] [--width 4] [--distance 3] "
                          L"[--head-locked] [--log FILE] "
                          L"[--screenshot FILE.bmp] [--probe-only] [--control-self-test] "
                          L"[--native-stereo --native-color-helper FILE.dll "
                          L"--native-color-mapping NAME]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown command-line option");
        }
    }
    if (!(options.durationSeconds >= 0.0 && options.durationSeconds <= 86400.0))
        throw std::runtime_error("--duration must be in [0, 86400]; zero waits until stop/event/runtime exit");
    if (!(options.waitWindowSeconds >= 0.0 && options.waitWindowSeconds <= 3600.0))
        throw std::runtime_error("--wait-window must be in [0, 3600]");
    if (!(options.widthMetres > 0.1f && options.widthMetres <= 20.0f))
        throw std::runtime_error("--width must be in (0.1, 20]");
    if (!(options.distanceMetres > 0.1f && options.distanceMetres <= 20.0f))
        throw std::runtime_error("--distance must be in (0.1, 20]");
    return options;
}

DWORD findProcessId(const std::wstring& requested) {
    const std::wstring wanted = lower(basename(requested));
    const HANDLE raw = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (raw == INVALID_HANDLE_VALUE) return 0;
    struct CloseHandleGuard { HANDLE value; ~CloseHandleGuard() { CloseHandle(value); } } guard{raw};
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!Process32FirstW(raw, &entry)) return 0;
    do {
        if (lower(entry.szExeFile) == wanted ||
            (wanted.find(L'.') == std::wstring::npos && lower(entry.szExeFile) == wanted + L".exe")) {
            return entry.th32ProcessID;
        }
    } while (Process32NextW(raw, &entry));
    return 0;
}

struct WindowCandidate {
    DWORD pid = 0;
    HWND hwnd = nullptr;
    uint64_t area = 0;
    Logger* logger = nullptr;
    bool emitDiagnostics = false;
    bool restoreWindow = false;
};

BOOL CALLBACK enumWindow(HWND hwnd, LPARAM param) {
    auto& best = *reinterpret_cast<WindowCandidate*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != best.pid) return TRUE;
    const int titleLength = GetWindowTextLengthW(hwnd);
    std::wstring title(static_cast<size_t>(std::max(0, titleLength)) + 1, L'\0');
    const int copied = titleLength > 0 ? GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size())) : 0;
    title.resize(static_cast<size_t>(std::max(0, copied)));
    const bool visible = IsWindowVisible(hwnd) != FALSE;
    const bool iconic = IsIconic(hwnd) != FALSE;
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))))
        GetWindowRect(hwnd, &rect);
    const auto width = std::max<LONG>(0, rect.right - rect.left);
    const auto height = std::max<LONG>(0, rect.bottom - rect.top);
    const uint64_t area = static_cast<uint64_t>(width) * height;
    if (best.emitDiagnostics && best.logger) {
        best.logger->line("window_candidate pid=", pid, " hwnd=0x", std::hex,
                          reinterpret_cast<uintptr_t>(hwnd), std::dec,
                          " title=\"", utf8(title), "\" visible=", visible ? "true" : "false",
                          " iconic=", iconic ? "true" : "false", " bounds=",
                          rect.left, ",", rect.top, "-", rect.right, ",", rect.bottom,
                          " size=", width, "x", height);
    }
    if (best.restoreWindow && iconic && !title.empty() && width > 0 && height > 0) {
        ShowWindowAsync(hwnd, SW_RESTORE);
        if (best.emitDiagnostics && best.logger)
            best.logger->line("window_restore_requested pid=", pid, " hwnd=0x", std::hex,
                              reinterpret_cast<uintptr_t>(hwnd), std::dec);
    }
    // Reject launchers/splash helpers and wait for a stable, game-sized HWND.
    if (visible && !iconic && !title.empty() && width >= 640 && height >= 360 && area > best.area) {
        best.hwnd = hwnd;
        best.area = area;
    }
    return TRUE;
}

HWND findGameWindow(DWORD pid, Logger* logger, bool emitDiagnostics, bool restoreWindow) {
    WindowCandidate candidate{pid, nullptr, 0, logger, emitDiagnostics, restoreWindow};
    EnumWindows(enumWindow, reinterpret_cast<LPARAM>(&candidate));
    return candidate.hwnd;
}

RECT windowBounds(HWND hwnd);

struct TargetWindow {
    DWORD pid = 0;
    HWND hwnd = nullptr;
    bool cancelled = false;
};

TargetWindow waitForGameWindow(const Options& options, Logger& log, HANDLE stopEvent) {
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(options.waitWindowSeconds));
    DWORD lastPid = 0;
    HWND stableHwnd = nullptr;
    RECT stableBounds{};
    uint32_t stableObservations = 0;
    auto nextDiagnostics = started;
    log.line("startup_wait_begin process=", utf8(options.process),
             " wait_window_seconds=", options.waitWindowSeconds,
             " restore_window=", options.restoreWindow ? "true" : "false");

    do {
        if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
            log.line("startup_wait_cancelled=stop_event");
            return {0, nullptr, true};
        }
        const auto now = std::chrono::steady_clock::now();
        const bool emitDiagnostics = now >= nextDiagnostics;
        if (emitDiagnostics) nextDiagnostics = now + std::chrono::seconds(5);
        const DWORD pid = findProcessId(options.process);
        if (pid != lastPid) {
            log.line("startup_process_state pid=", pid, pid ? " found=true" : " found=false");
            lastPid = pid;
            stableHwnd = nullptr;
            stableObservations = 0;
        }
        if (pid) {
            const HWND hwnd = findGameWindow(pid, &log, emitDiagnostics, options.restoreWindow);
            if (hwnd) {
                const RECT bounds = windowBounds(hwnd);
                const bool same = hwnd == stableHwnd && EqualRect(&bounds, &stableBounds);
                stableHwnd = hwnd;
                stableBounds = bounds;
                stableObservations = same ? stableObservations + 1 : 1;
                if (stableObservations >= 3) {
                    log.line("startup_window_ready pid=", pid, " hwnd=0x", std::hex,
                             reinterpret_cast<uintptr_t>(hwnd), std::dec,
                             " stable_observations=", stableObservations, " bounds=",
                             bounds.left, ",", bounds.top, "-", bounds.right, ",", bounds.bottom);
                    return {pid, hwnd, false};
                }
            } else {
                stableHwnd = nullptr;
                stableObservations = 0;
            }
        }
        if (options.waitWindowSeconds == 0.0) break;
        if (WaitForSingleObject(stopEvent, 500) == WAIT_OBJECT_0) {
            log.line("startup_wait_cancelled=stop_event");
            return {0, nullptr, true};
        }
    } while (std::chrono::steady_clock::now() <= deadline);

    throw std::runtime_error("target process/game-sized visible window did not become ready before --wait-window timeout");
}

RECT windowBounds(HWND hwnd) {
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        if (!GetWindowRect(hwnd, &rect)) throw std::runtime_error("GetWindowRect failed");
    }
    return rect;
}

std::string luidString(const LUID& luid) {
    std::ostringstream s;
    s << std::hex << std::uppercase << static_cast<uint32_t>(luid.HighPart) << ':' << luid.LowPart;
    return s.str();
}

bool sameLuid(const LUID& a, const LUID& b) {
    return a.HighPart == b.HighPart && a.LowPart == b.LowPart;
}

struct XrHandles {
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE;
    XrSpace viewSpace = XR_NULL_HANDLE;
    std::array<XrSwapchain, 2> swapchains{XR_NULL_HANDLE, XR_NULL_HANDLE};
    ~XrHandles() {
        for (auto swapchain : swapchains) if (swapchain) xrDestroySwapchain(swapchain);
        if (viewSpace) xrDestroySpace(viewSpace);
        if (localSpace) xrDestroySpace(localSpace);
        if (session) xrDestroySession(session);
        if (instance) xrDestroyInstance(instance);
    }
};

struct EyeGazeSample final {
    bool actualValid = false;
    bool actionActive = false;
    bool orientationTracked = false;
    XrQuaternionf orientation{0, 0, 0, 1};
    XrVector3f position{0, 0, 0};
    XrTime requestedTime = 0;
    XrTime sampleTime = 0;
    XrDuration sampleAge = 0;
    XrSpaceLocationFlags locationFlags = 0;
    XrResult lastResult = XR_SUCCESS;
};

class EyeGazeTracker {
public:
    ~EyeGazeTracker() { reset(); }

    bool initialize(XrInstance instance, XrSession session, XrSpace localSpace,
                    bool extensionEnabled, bool systemSupported, Logger& log) {
        localSpace_ = localSpace;
        if (!extensionEnabled || !systemSupported) {
            log.line("eye_gaze_tracker=disabled extension_enabled=", extensionEnabled ? "true" : "false",
                     " system_supported=", systemSupported ? "true" : "false",
                     " fallback=head_forward_not_eye_gaze vrs_active=false");
            return false;
        }

        XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::snprintf(setInfo.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "%s", "eye_gaze");
        std::snprintf(setInfo.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE,
                      "%s", "Eye gaze");
        setInfo.priority = 0;
        if (!ok(xrCreateActionSet(instance, &setInfo, &actionSet_), "xrCreateActionSet", log)) return false;

        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
        actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
        std::snprintf(actionInfo.actionName, XR_MAX_ACTION_NAME_SIZE, "%s", "gaze_pose");
        std::snprintf(actionInfo.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE,
                      "%s", "Gaze pose");
        if (!ok(xrCreateAction(actionSet_, &actionInfo, &action_), "xrCreateAction(gaze)", log)) return false;

        XrPath profile = XR_NULL_PATH;
        XrPath gazePose = XR_NULL_PATH;
        if (!ok(xrStringToPath(instance, "/interaction_profiles/ext/eye_gaze_interaction", &profile),
                "xrStringToPath(eye gaze profile)", log) ||
            !ok(xrStringToPath(instance, "/user/eyes_ext/input/gaze_ext/pose", &gazePose),
                "xrStringToPath(gaze pose)", log)) return false;
        const XrActionSuggestedBinding binding{action_, gazePose};
        XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggested.interactionProfile = profile;
        suggested.suggestedBindings = &binding;
        suggested.countSuggestedBindings = 1;
        if (!ok(xrSuggestInteractionProfileBindings(instance, &suggested),
                "xrSuggestInteractionProfileBindings(eye gaze)", log)) return false;

        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action = action_;
        spaceInfo.poseInActionSpace = {{0, 0, 0, 1}, {0, 0, 0}};
        if (!ok(xrCreateActionSpace(session, &spaceInfo, &gazeSpace_),
                "xrCreateActionSpace(gaze)", log)) return false;
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets = 1;
        attach.actionSets = &actionSet_;
        if (!ok(xrAttachSessionActionSets(session, &attach),
                "xrAttachSessionActionSets(gaze)", log)) return false;
        enabled_ = true;
        log.line("eye_gaze_tracker=enabled profile=/interaction_profiles/ext/eye_gaze_interaction",
                 " binding=/user/eyes_ext/input/gaze_ext/pose semantic=actual_eye_gaze",
                 " vrs_active=false");
        return true;
    }

    EyeGazeSample sample(XrSession session, XrTime sourcePoseTime, bool sessionFocused) {
        EyeGazeSample result;
        result.requestedTime = sourcePoseTime;
        if (!enabled_ || !sessionFocused || sourcePoseTime == 0) {
            result.lastResult = enabled_ ? XR_SESSION_NOT_FOCUSED : XR_ERROR_FEATURE_UNSUPPORTED;
            return result;
        }
        const XrActiveActionSet active{actionSet_, XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
        sync.countActiveActionSets = 1;
        sync.activeActionSets = &active;
        result.lastResult = xrSyncActions(session, &sync);
        if (XR_FAILED(result.lastResult)) return result;

        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action_;
        XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
        result.lastResult = xrGetActionStatePose(session, &getInfo, &state);
        if (XR_FAILED(result.lastResult)) return result;
        result.actionActive = state.isActive == XR_TRUE;
        if (!result.actionActive) return result;

        XrEyeGazeSampleTimeEXT sampleTime{XR_TYPE_EYE_GAZE_SAMPLE_TIME_EXT};
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION, &sampleTime};
        result.lastResult = xrLocateSpace(gazeSpace_, localSpace_, sourcePoseTime, &location);
        result.locationFlags = location.locationFlags;
        result.orientationTracked =
            (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
        result.orientation = location.pose.orientation;
        result.position = location.pose.position;
        result.sampleTime = sampleTime.time;
        if (XR_FAILED(result.lastResult) ||
            (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0 ||
            !result.orientationTracked || result.sampleTime == 0)
            return result;
        result.sampleAge = result.sampleTime >= sourcePoseTime
            ? result.sampleTime - sourcePoseTime
            : sourcePoseTime - result.sampleTime;
        constexpr XrDuration kMaximumSampleAge = 100'000'000; // 100 ms, OpenXR time is ns.
        const float q2 = result.orientation.x * result.orientation.x +
                         result.orientation.y * result.orientation.y +
                         result.orientation.z * result.orientation.z +
                         result.orientation.w * result.orientation.w;
        if (!std::isfinite(q2) || q2 < 0.95f || q2 > 1.05f ||
            result.sampleAge > kMaximumSampleAge)
            return result;
        result.actualValid = true;
        return result;
    }

    bool enabled() const { return enabled_; }

private:
    bool ok(XrResult result, const char* operation, Logger& log) {
        if (XR_SUCCEEDED(result)) return true;
        log.line("eye_gaze_setup_failed=", operation, " result=", result,
                 " fallback=head_forward_not_eye_gaze vrs_active=false");
        reset();
        return false;
    }

    void reset() {
        enabled_ = false;
        if (gazeSpace_) xrDestroySpace(gazeSpace_);
        if (action_) xrDestroyAction(action_);
        if (actionSet_) xrDestroyActionSet(actionSet_);
        gazeSpace_ = XR_NULL_HANDLE;
        action_ = XR_NULL_HANDLE;
        actionSet_ = XR_NULL_HANDLE;
    }

    XrSpace localSpace_ = XR_NULL_HANDLE;
    XrActionSet actionSet_ = XR_NULL_HANDLE;
    XrAction action_ = XR_NULL_HANDLE;
    XrSpace gazeSpace_ = XR_NULL_HANDLE;
    bool enabled_ = false;
};

struct D3DContext {
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 adapterDesc{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
};

D3DContext createDeviceForLuid(const LUID& luid, D3D_FEATURE_LEVEL minimumFeatureLevel) {
    ComPtr<IDXGIFactory1> factory;
    checkHr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");

    D3DContext out;
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        if (factory->EnumAdapters1(index, &candidate) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        candidate->GetDesc1(&desc);
        if (sameLuid(desc.AdapterLuid, luid)) {
            out.adapter = candidate;
            out.adapterDesc = desc;
            break;
        }
    }
    if (!out.adapter) throw std::runtime_error("OpenXR adapter LUID was not found by DXGI");

    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL actual{};
    checkHr(D3D11CreateDevice(out.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, _countof(levels),
                             D3D11_SDK_VERSION, &out.device, &actual, &out.context),
            "D3D11CreateDevice");
    if (actual < minimumFeatureLevel) throw std::runtime_error("D3D feature level below OpenXR requirement");
    return out;
}

struct CaptureSource {
    ComPtr<ID3D11ShaderResourceView> srv;
    uint64_t frames = 0;
    RECT crop{};
    uint32_t sourceWidth = 0;
    uint32_t sourceHeight = 0;
    virtual ~CaptureSource() = default;
    virtual bool acquire(ID3D11DeviceContext* context, HWND hwnd) = 0;
    virtual const char* name() const = 0;
};

struct DesktopCapture final : CaptureSource {
    ComPtr<IDXGIOutputDuplication> duplication;
    DXGI_OUTDUPL_DESC duplicateDesc{};
    DXGI_OUTPUT_DESC outputDesc{};
    ComPtr<ID3D11Texture2D> texture;

    void initialize(ID3D11Device* device, IDXGIAdapter1* adapter, HWND hwnd) {
        const RECT game = windowBounds(hwnd);
        const POINT centre{(game.left + game.right) / 2, (game.top + game.bottom) / 2};
        ComPtr<IDXGIOutput> selected;
        uint64_t bestOverlap = 0;
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(i, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC desc{};
            output->GetDesc(&desc);
            RECT overlap{};
            const bool intersects = IntersectRect(&overlap, &game, &desc.DesktopCoordinates) != FALSE;
            const uint64_t area = intersects ? static_cast<uint64_t>(overlap.right - overlap.left) *
                                                 static_cast<uint64_t>(overlap.bottom - overlap.top) : 0;
            if (PtInRect(&desc.DesktopCoordinates, centre) || area > bestOverlap) {
                selected = output;
                outputDesc = desc;
                bestOverlap = area;
                if (PtInRect(&desc.DesktopCoordinates, centre)) break;
            }
        }
        if (!selected) throw std::runtime_error("game window is not on an output owned by the OpenXR GPU");

        ComPtr<IDXGIOutput1> output1;
        checkHr(selected.As(&output1), "IDXGIOutput1 query");
        checkHr(output1->DuplicateOutput(device, &duplication), "DuplicateOutput");
        duplication->GetDesc(&duplicateDesc);

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = duplicateDesc.ModeDesc.Width;
        desc.Height = duplicateDesc.ModeDesc.Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = duplicateDesc.ModeDesc.Format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        checkHr(device->CreateTexture2D(&desc, nullptr, &texture), "Create capture texture");
        checkHr(device->CreateShaderResourceView(texture.Get(), nullptr, &srv), "Create capture SRV");
        sourceWidth = desc.Width;
        sourceHeight = desc.Height;
        updateCrop(hwnd);
    }

    void updateCrop(HWND hwnd) {
        RECT game = windowBounds(hwnd);
        RECT clipped{};
        if (!IntersectRect(&clipped, &game, &outputDesc.DesktopCoordinates))
            throw std::runtime_error("game window no longer overlaps the captured output");
        crop.left = clipped.left - outputDesc.DesktopCoordinates.left;
        crop.top = clipped.top - outputDesc.DesktopCoordinates.top;
        crop.right = clipped.right - outputDesc.DesktopCoordinates.left;
        crop.bottom = clipped.bottom - outputDesc.DesktopCoordinates.top;
    }

    bool acquire(ID3D11DeviceContext* context, HWND hwnd) {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        const HRESULT hr = duplication->AcquireNextFrame(0, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
        if (hr == DXGI_ERROR_ACCESS_LOST) throw std::runtime_error("desktop duplication access lost");
        checkHr(hr, "AcquireNextFrame");
        ComPtr<ID3D11Texture2D> desktop;
        checkHr(resource.As(&desktop), "desktop texture query");
        context->CopyResource(texture.Get(), desktop.Get());
        checkHr(duplication->ReleaseFrame(), "ReleaseFrame");
        updateCrop(hwnd);
        ++frames;
        return true;
    }

    const char* name() const override { return "desktop_duplication_fallback"; }
};

struct WindowGraphicsCapture final : CaptureSource {
    using Direct3DDevice = winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
    using FramePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
    using CaptureItem = winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
    using CaptureSession = winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
    using PixelFormat = winrt::Windows::Graphics::DirectX::DirectXPixelFormat;

    ID3D11Device* device = nullptr;
    Direct3DDevice winrtDevice{nullptr};
    CaptureItem item{nullptr};
    FramePool framePool{nullptr};
    CaptureSession session{nullptr};
    ComPtr<ID3D11Texture2D> texture;
    winrt::Windows::Graphics::SizeInt32 poolSize{};

    ~WindowGraphicsCapture() override {
        if (session) session.Close();
        if (framePool) framePool.Close();
    }

    void initialize(ID3D11Device* d3dDevice, HWND hwnd) {
        if (!CaptureSession::IsSupported())
            throw std::runtime_error("Windows Graphics Capture is not supported");
        device = d3dDevice;
        ComPtr<IDXGIDevice> dxgiDevice;
        checkHr(d3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice)), "Query IDXGIDevice for WGC");
        winrt::com_ptr<IInspectable> inspectable;
        checkHr(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put()),
                "CreateDirect3D11DeviceFromDXGIDevice");
        winrtDevice = inspectable.as<Direct3DDevice>();

        auto interop = winrt::get_activation_factory<CaptureItem, IGraphicsCaptureItemInterop>();
        checkHr(interop->CreateForWindow(hwnd, winrt::guid_of<CaptureItem>(), winrt::put_abi(item)),
                "GraphicsCaptureItem::CreateForWindow");
        poolSize = item.Size();
        if (poolSize.Width <= 0 || poolSize.Height <= 0)
            throw std::runtime_error("Windows Graphics Capture item has zero size");
        framePool = FramePool::CreateFreeThreaded(winrtDevice, PixelFormat::B8G8R8A8UIntNormalized,
                                                   2, poolSize);
        session = framePool.CreateCaptureSession(item);
        session.IsCursorCaptureEnabled(false);
        recreateTexture(static_cast<uint32_t>(poolSize.Width), static_cast<uint32_t>(poolSize.Height));
        session.StartCapture();
    }

    void recreateTexture(uint32_t width, uint32_t height) {
        srv.Reset();
        texture.Reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        checkHr(device->CreateTexture2D(&desc, nullptr, &texture), "Create WGC capture texture");
        checkHr(device->CreateShaderResourceView(texture.Get(), nullptr, &srv), "Create WGC capture SRV");
        sourceWidth = width;
        sourceHeight = height;
        crop = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    }

    bool acquire(ID3D11DeviceContext* context, HWND) override {
        auto frame = framePool.TryGetNextFrame();
        if (!frame) return false;
        const auto contentSize = frame.ContentSize();
        if (contentSize.Width <= 0 || contentSize.Height <= 0) {
            frame.Close();
            return false;
        }
        if (contentSize.Width != poolSize.Width || contentSize.Height != poolSize.Height) {
            frame.Close();
            poolSize = contentSize;
            framePool.Recreate(winrtDevice, PixelFormat::B8G8R8A8UIntNormalized, 2, poolSize);
            recreateTexture(static_cast<uint32_t>(poolSize.Width), static_cast<uint32_t>(poolSize.Height));
            return false;
        }

        auto access = frame.Surface().as<
            ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> frameTexture;
        checkHr(access->GetInterface(IID_PPV_ARGS(&frameTexture)), "Get WGC ID3D11Texture2D");
        D3D11_TEXTURE2D_DESC frameDesc{};
        frameTexture->GetDesc(&frameDesc);
        if (frameDesc.Width < sourceWidth || frameDesc.Height < sourceHeight) {
            frame.Close();
            throw std::runtime_error("WGC frame surface is smaller than its reported content size");
        }
        const D3D11_BOX box{0, 0, 0, sourceWidth, sourceHeight, 1};
        context->CopySubresourceRegion(texture.Get(), 0, 0, 0, 0, frameTexture.Get(), 0, &box);
        frame.Close();
        ++frames;
        return true;
    }

    const char* name() const override { return "windows_graphics_capture_hwnd"; }
};

class NativeStereoConsumer {
public:
    using Frame = anvilvr::shadows_native_color::ConsumerFrame;
    using Metadata = anvilvr::shadows_native_color::EyeFrameMetadata;

    NativeStereoConsumer(const std::filesystem::path& helperPath, const std::wstring& mappingName,
                         ID3D11Device* device, ID3D11DeviceContext* context, Logger& log)
        : context_(context), log_(log), mappingName_(mappingName) {
        const auto absolute = std::filesystem::absolute(helperPath);
        module_ = LoadLibraryW(absolute.c_str());
        if (!module_)
            throw std::runtime_error("LoadLibraryW(native color helper) failed for " + absolute.string());
        create_ = load<CreateFn>("AnvilShadowsNativeColor_CreateConsumer");
        acquire_ = load<AcquireFn>("AnvilShadowsNativeColor_AcquireLatestEye");
        release_ = load<ReleaseFn>("AnvilShadowsNativeColor_ReleaseEye");
        destroy_ = load<DestroyFn>("AnvilShadowsNativeColor_DestroyConsumer");
        const auto status = create_(mappingName.c_str(), device, context, &consumer_);
        if (status != anvilvr::shadows_native_color::kOk) {
            std::ostringstream message;
            message << "native color CreateConsumer failed status=" << status;
            throw std::runtime_error(message.str());
        }
        log_.line("native_stereo_consumer=true helper=", absolute.string(),
                  " mapping=", utf8(mappingName), " fallback=disabled");
    }

    ~NativeStereoConsumer() {
        for (auto& eye : eyes_) {
            eye.srv.Reset();
            eye.texture.Reset();
        }
        for (auto& pendingEye : pending_)
            for (auto& frame : pendingEye) {
                frame.srv.Reset();
                frame.texture.Reset();
            }
        if (consumer_ && destroy_) destroy_(consumer_);
        consumer_ = nullptr;
        if (module_) FreeLibrary(module_);
    }

    NativeStereoConsumer(const NativeStereoConsumer&) = delete;
    NativeStereoConsumer& operator=(const NativeStereoConsumer&) = delete;

    bool acquireLatest(std::uint32_t eye) {
        if (eye >= eyes_.size()) return false;
        if (!consumer_ && !tryRecoverConsumer()) return false;
        Frame frame{};
        const auto status = acquire_(consumer_, eye, &frame);
        if (status == anvilvr::shadows_native_color::kNoPublishedFrame) {
            ++noFrame_[eye];
            return false;
        }
        if (status != anvilvr::shadows_native_color::kOk) {
            ++errors_[eye];
            lastError_[eye] = status;
            if (status == anvilvr::shadows_native_color::kGenerationChanged)
                beginGenerationRecovery();
            return false;
        }

        const bool isNew = frame.publicationSequence != latestAcquiredPublication_[eye];
        if (isNew) {
            ComPtr<ID3D11Texture2D> source;
            const HRESULT queryResult = frame.texture->QueryInterface(IID_PPV_ARGS(&source));
            if (SUCCEEDED(queryResult) && validMetadata(frame.metadata, eye)) {
                D3D11_TEXTURE2D_DESC sourceDesc{};
                source->GetDesc(&sourceDesc);
                Pending pending;
                if (prepareTexture(pending, sourceDesc, frame.width, frame.height, frame.format, eye)) {
                    context_->CopyResource(pending.texture.Get(), source.Get());
                    pending.metadata = frame.metadata;
                    pending.publicationSequence = frame.publicationSequence;
                    pending.width = frame.width;
                    pending.height = frame.height;
                    pending.format = frame.format;
                    pending_[eye].push_back(std::move(pending));
                    latestAcquiredPublication_[eye] = frame.publicationSequence;
                    trimPending(eye);
                    ++fresh_[eye];
                }
            } else {
                ++errors_[eye];
                lastError_[eye] = FAILED(queryResult)
                    ? anvilvr::shadows_native_color::kQueryInterfaceFailed
                    : anvilvr::shadows_native_color::kInvalidArgument;
            }
        } else {
            ++stale_[eye];
        }
        const auto releaseStatus = release_(consumer_, &frame);
        if (releaseStatus != anvilvr::shadows_native_color::kOk) {
            ++errors_[eye];
            lastError_[eye] = releaseStatus;
        }
        return isNew;
    }

    bool commitLatestPair() {
        std::vector<anvilvr::shadows_stereo_pair::Observation> observations;
        for (std::uint32_t eye = 0; eye < pending_.size(); ++eye) {
            for (std::size_t index = 0; index < pending_[eye].size(); ++index) {
                const auto& frame = pending_[eye][index];
                const auto stamp = anvilvr::shadows_stereo_pair::makeStamp(
                    frame.metadata.sourceFrame, frame.metadata.poseSequence,
                    frame.metadata.predictedDisplayTime, frame.publicationSequence,
                    frame.metadata.reserved);
                if (stamp) observations.push_back({eye, index, *stamp});
            }
        }
        const auto match = anvilvr::shadows_stereo_pair::newestMatchedPair(observations);
        if (!match || !anvilvr::shadows_stereo_pair::isNewerThanCommitted(
                          *match, hasCommittedPair_, committedPairExplicit_, committedPairId_)) {
            if (!pending_[0].empty() || !pending_[1].empty()) ++pairWaits_;
            return false;
        }

        std::array<Pending, 2> selected{
            std::move(pending_[0][match->tokens[0]]),
            std::move(pending_[1][match->tokens[1]])};
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            recycleCommitted(eye);
            eyes_[eye].texture = std::move(selected[eye].texture);
            eyes_[eye].srv = std::move(selected[eye].srv);
            eyes_[eye].metadata = selected[eye].metadata;
            eyes_[eye].publicationSequence = selected[eye].publicationSequence;
            eyes_[eye].width = selected[eye].width;
            eyes_[eye].height = selected[eye].height;
            eyes_[eye].format = selected[eye].format;
        }
        committedPairId_ = match->stamp.pairId;
        committedPairExplicit_ = match->stamp.explicitPairId;
        hasCommittedPair_ = true;
        ++pairedCommits_;

        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            auto& queue = pending_[eye];
            for (auto it = queue.begin(); it != queue.end();) {
                const auto stamp = anvilvr::shadows_stereo_pair::makeStamp(
                    it->metadata.sourceFrame, it->metadata.poseSequence,
                    it->metadata.predictedDisplayTime, it->publicationSequence,
                    it->metadata.reserved);
                if (!stamp || stamp->explicitPairId != committedPairExplicit_ ||
                    stamp->pairId <= committedPairId_) {
                    recyclePending(eye, *it);
                    it = queue.erase(it);
                } else {
                    ++it;
                }
            }
        }
        return true;
    }

    bool ready() const { return eyes_[0].texture && eyes_[1].texture; }
    void resolveContentCropFromEye0() {
        if (contentCropResolved_ || !eyes_[0].texture ||
            cropAttemptSequence_ == eyes_[0].publicationSequence)
            return;
        cropAttemptSequence_ = eyes_[0].publicationSequence;

        D3D11_TEXTURE2D_DESC desc{};
        eyes_[0].texture->GetDesc(&desc);
        if (desc.SampleDesc.Count != 1 ||
            (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
             desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)) {
            contentCropResolved_ = true;
            log_.line("native_content_crop=full reason=unsupported_probe_format format=",
                      static_cast<std::uint32_t>(desc.Format));
            return;
        }

        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.BindFlags = 0;
        stagingDesc.MiscFlags = 0;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(deviceFromContext()->CreateTexture2D(&stagingDesc, nullptr, &staging))) return;
        context_->CopyResource(staging.Get(), eyes_[0].texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;

        const std::uint32_t rowStep = std::max(1u, desc.Height / 96u);
        const std::uint32_t columnStep = std::max(1u, desc.Width / 160u);
        std::vector<std::uint16_t> columnBright(desc.Width, 0);
        std::vector<std::uint16_t> rowBright(desc.Height, 0);
        std::uint64_t sampled = 0;
        std::uint64_t bright = 0;
        const auto pixelBright = [](const std::uint8_t* pixel) {
            return pixel[0] > 8 || pixel[1] > 8 || pixel[2] > 8;
        };
        for (std::uint32_t y = 0; y < desc.Height; y += rowStep) {
            const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
                              static_cast<std::size_t>(y) * mapped.RowPitch;
            for (std::uint32_t x = 0; x < desc.Width; ++x) {
                if (pixelBright(row + static_cast<std::size_t>(x) * 4u)) ++columnBright[x];
            }
        }
        for (std::uint32_t y = 0; y < desc.Height; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
                              static_cast<std::size_t>(y) * mapped.RowPitch;
            for (std::uint32_t x = 0; x < desc.Width; x += columnStep) {
                const bool nonBlack = pixelBright(row + static_cast<std::size_t>(x) * 4u);
                if (nonBlack) {
                    ++rowBright[y];
                    ++bright;
                }
                ++sampled;
            }
        }
        context_->Unmap(staging.Get(), 0);

        // A fully black loading frame is not evidence for a crop. Wait for the
        // first actual picture, then resolve exactly once for this session.
        if (sampled == 0 || bright * 100u < sampled) return;

        const std::uint32_t columnSamples = (desc.Height + rowStep - 1u) / rowStep;
        const std::uint32_t rowSamples = (desc.Width + columnStep - 1u) / columnStep;
        const std::uint32_t columnLimit = std::max(1u, columnSamples / 50u);
        const std::uint32_t rowLimit = std::max(1u, rowSamples / 50u);
        std::uint32_t left = 0, right = desc.Width, top = 0, bottom = desc.Height;
        while (left < right && columnBright[left] <= columnLimit) ++left;
        while (right > left && columnBright[right - 1u] <= columnLimit) --right;
        while (top < bottom && rowBright[top] <= rowLimit) ++top;
        while (bottom > top && rowBright[bottom - 1u] <= rowLimit) --bottom;

        const std::uint32_t leftBar = left;
        const std::uint32_t rightBar = desc.Width - right;
        const std::uint32_t topBar = top;
        const std::uint32_t bottomBar = desc.Height - bottom;
        const auto pairedBars = [](std::uint32_t first, std::uint32_t second,
                                   std::uint32_t extent, std::uint32_t maxPercent) {
            const std::uint32_t minBar = std::max(4u, extent / 100u);
            const std::uint32_t maxBar = extent * maxPercent / 100u;
            const std::uint32_t tolerance = std::max(12u, extent / 50u);
            return first >= minBar && second >= minBar && first <= maxBar && second <= maxBar &&
                   (first > second ? first - second : second - first) <= tolerance;
        };
        if (!pairedBars(leftBar, rightBar, desc.Width, 35u)) {
            left = 0;
            right = desc.Width;
        } else {
            left = left > 2u ? left - 2u : 0u;
            right = std::min(desc.Width, right + 2u);
        }
        if (!pairedBars(topBar, bottomBar, desc.Height, 25u)) {
            top = 0;
            bottom = desc.Height;
        } else {
            top = top > 2u ? top - 2u : 0u;
            bottom = std::min(desc.Height, bottom + 2u);
        }

        contentUv_ = {
            static_cast<float>(right - left) / static_cast<float>(desc.Width),
            static_cast<float>(bottom - top) / static_cast<float>(desc.Height),
            static_cast<float>(left) / static_cast<float>(desc.Width),
            static_cast<float>(top) / static_cast<float>(desc.Height)};
        contentCropResolved_ = true;
        log_.line("native_content_crop=resolved source=", desc.Width, "x", desc.Height,
                  " pixels=", left, ",", top, "-", right, ",", bottom,
                  " bars_lrtb=", leftBar, ",", rightBar, ",", topBar, ",", bottomBar,
                  " uv_scale_offset=", contentUv_[0], ",", contentUv_[1], ",",
                  contentUv_[2], ",", contentUv_[3]);
    }
    const std::array<float, 4>& contentUv() const { return contentUv_; }
    ID3D11ShaderResourceView* srv(std::uint32_t eye) const { return eyes_[eye].srv.Get(); }
    const Metadata& metadata(std::uint32_t eye) const { return eyes_[eye].metadata; }
    std::uint64_t sourceFrame(std::uint32_t eye) const { return eyes_[eye].metadata.sourceFrame; }
    std::uint64_t poseSequence(std::uint32_t eye) const { return eyes_[eye].metadata.poseSequence; }
    std::uint64_t fresh(std::uint32_t eye) const { return fresh_[eye]; }
    std::uint64_t stale(std::uint32_t eye) const { return stale_[eye]; }
    std::uint64_t noFrame(std::uint32_t eye) const { return noFrame_[eye]; }
    std::uint64_t errors(std::uint32_t eye) const { return errors_[eye]; }
    std::uint32_t lastError(std::uint32_t eye) const { return lastError_[eye]; }
    std::uint64_t pairedCommits() const { return pairedCommits_; }
    std::uint64_t pairWaits() const { return pairWaits_; }
    std::uint64_t currentPair() const { return committedPairId_; }
    bool currentPairExplicit() const { return committedPairExplicit_; }
    std::uint64_t generationChanges() const { return generationChanges_; }
    std::uint64_t recoveryAttempts() const { return recoveryAttempts_; }
    std::uint64_t recoverySuccesses() const { return recoverySuccesses_; }
    bool recoveryActive() const { return recoveryBackoff_.active(); }

private:
    struct Eye {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        Metadata metadata{};
        std::uint64_t publicationSequence = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t format = 0;
    };
    struct Pending : Eye {};
    using CreateFn = std::uint32_t (*)(const wchar_t*, IUnknown*, IUnknown*, void**);
    using AcquireFn = std::uint32_t (*)(void*, std::uint32_t, Frame*);
    using ReleaseFn = std::uint32_t (*)(void*, Frame*);
    using DestroyFn = void (*)(void*);

    template <typename T> T load(const char* name) {
        const auto address = GetProcAddress(module_, name);
        if (!address) throw std::runtime_error(std::string("missing native color helper export: ") + name);
        return reinterpret_cast<T>(address);
    }

    ID3D11Device* deviceFromContext() {
        if (!device_) context_->GetDevice(&device_);
        return device_.Get();
    }

    static std::uint64_t steadyMilliseconds() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void clearGenerationState() {
        context_->Flush();
        for (auto& eye : eyes_) eye = {};
        for (auto& queue : pending_) queue.clear();
        for (auto& pool : pool_) pool.clear();
        latestAcquiredPublication_.fill(0);
        committedPairId_ = 0;
        committedPairExplicit_ = false;
        hasCommittedPair_ = false;
        contentUv_ = {1.0f, 1.0f, 0.0f, 0.0f};
        cropAttemptSequence_ = 0;
        contentCropResolved_ = false;
    }

    void beginGenerationRecovery() {
        if (consumer_) {
            destroy_(consumer_);
            consumer_ = nullptr;
        }
        clearGenerationState();
        recoveryBackoff_.begin(steadyMilliseconds());
        ++generationChanges_;
        log_.line("native_consumer_generation_changed=true count=", generationChanges_,
                  " old_pair_discarded=true");
    }

    bool tryRecoverConsumer() {
        const auto now = steadyMilliseconds();
        if (!recoveryBackoff_.canAttempt(now)) return false;
        ++recoveryAttempts_;
        void* reopened = nullptr;
        const auto status = create_(mappingName_.c_str(), deviceFromContext(), context_, &reopened);
        if (status == anvilvr::shadows_native_color::kOk && reopened) {
            consumer_ = reopened;
            recoveryBackoff_.succeeded();
            ++recoverySuccesses_;
            lastError_.fill(anvilvr::shadows_native_color::kOk);
            log_.line("native_consumer_recovery=success attempt=", recoveryAttempts_,
                      " successes=", recoverySuccesses_, " stale_pair_reused=false");
            return true;
        }
        if (reopened) destroy_(reopened);
        recoveryBackoff_.failed(now);
        const auto retryMilliseconds = recoveryBackoff_.nextAttemptMilliseconds() - now;
        log_.line("native_consumer_recovery=waiting attempt=", recoveryAttempts_,
                  " status=", status, " retry_ms=", retryMilliseconds);
        return false;
    }

    bool prepareTexture(Pending& destination, const D3D11_TEXTURE2D_DESC& sourceDesc,
                        std::uint32_t width, std::uint32_t height, std::uint32_t format,
                        std::uint32_t eye) {
        auto& pool = pool_[eye];
        for (auto it = pool.begin(); it != pool.end(); ++it) {
            if (it->width == width && it->height == height && it->format == format) {
                destination = std::move(*it);
                pool.erase(it);
                return true;
            }
        }
        D3D11_TEXTURE2D_DESC copyDesc = sourceDesc;
        copyDesc.MipLevels = 1;
        copyDesc.ArraySize = 1;
        copyDesc.SampleDesc = {1, 0};
        copyDesc.Usage = D3D11_USAGE_DEFAULT;
        copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copyDesc.CPUAccessFlags = 0;
        copyDesc.MiscFlags = 0;
        HRESULT hr = deviceFromContext()->CreateTexture2D(&copyDesc, nullptr, &destination.texture);
        if (SUCCEEDED(hr))
            hr = deviceFromContext()->CreateShaderResourceView(destination.texture.Get(), nullptr,
                                                               &destination.srv);
        if (FAILED(hr)) {
            destination.srv.Reset();
            destination.texture.Reset();
            ++errors_[eye];
            lastError_[eye] = anvilvr::shadows_native_color::kCreateFailed;
            return false;
        }
        destination.width = width;
        destination.height = height;
        destination.format = format;
        return true;
    }

    void recyclePending(std::uint32_t eye, Pending& frame) {
        if (!frame.texture) return;
        frame.metadata = {};
        frame.publicationSequence = 0;
        if (pool_[eye].size() < kPendingCapacity) pool_[eye].push_back(std::move(frame));
    }

    void recycleCommitted(std::uint32_t eye) {
        if (!eyes_[eye].texture) return;
        Pending old;
        static_cast<Eye&>(old) = std::move(eyes_[eye]);
        recyclePending(eye, old);
    }

    void trimPending(std::uint32_t eye) {
        while (pending_[eye].size() > kPendingCapacity) {
            recyclePending(eye, pending_[eye].front());
            pending_[eye].pop_front();
        }
    }

    static bool validMetadata(const Metadata& metadata, std::uint32_t eye) {
        if (metadata.size != sizeof(Metadata) || metadata.version != anvilvr::shadows_native_color::kAbiVersion ||
            metadata.eye != eye)
            return false;
        for (float value : metadata.orientation) if (!std::isfinite(value)) return false;
        for (float value : metadata.position) if (!std::isfinite(value)) return false;
        for (float value : metadata.fov) if (!std::isfinite(value)) return false;
        const float q2 = metadata.orientation[0] * metadata.orientation[0] +
                         metadata.orientation[1] * metadata.orientation[1] +
                         metadata.orientation[2] * metadata.orientation[2] +
                         metadata.orientation[3] * metadata.orientation[3];
        return q2 > 0.5f && q2 < 1.5f && metadata.fov[0] < metadata.fov[1] &&
               metadata.fov[3] < metadata.fov[2];
    }

    HMODULE module_ = nullptr;
    void* consumer_ = nullptr;
    CreateFn create_ = nullptr;
    AcquireFn acquire_ = nullptr;
    ReleaseFn release_ = nullptr;
    DestroyFn destroy_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ComPtr<ID3D11Device> device_;
    Logger& log_;
    std::wstring mappingName_;
    std::array<Eye, 2> eyes_{};
    static constexpr std::size_t kPendingCapacity = 4;
    std::array<std::deque<Pending>, 2> pending_{};
    std::array<std::vector<Pending>, 2> pool_{};
    std::array<std::uint64_t, 2> latestAcquiredPublication_{};
    std::array<std::uint64_t, 2> fresh_{};
    std::array<std::uint64_t, 2> stale_{};
    std::array<std::uint64_t, 2> noFrame_{};
    std::array<std::uint64_t, 2> errors_{};
    std::array<std::uint32_t, 2> lastError_{};
    std::uint64_t pairedCommits_ = 0;
    std::uint64_t pairWaits_ = 0;
    std::uint64_t committedPairId_ = 0;
    bool committedPairExplicit_ = false;
    bool hasCommittedPair_ = false;
    anvilvr::shadows_consumer_recovery::Backoff recoveryBackoff_{};
    std::uint64_t generationChanges_ = 0;
    std::uint64_t recoveryAttempts_ = 0;
    std::uint64_t recoverySuccesses_ = 0;
    std::array<float, 4> contentUv_{1.0f, 1.0f, 0.0f, 0.0f};
    std::uint64_t cropAttemptSequence_ = 0;
    bool contentCropResolved_ = false;
};

struct Blitter {
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Buffer> constants;

    void initialize(ID3D11Device* device) {
        static constexpr char shader[] = R"(
cbuffer Transform : register(b0) { row_major float4x4 worldViewProjection; float4 uvScaleOffset; };
Texture2D sourceTexture : register(t0);
SamplerState linearClamp : register(s0);
struct VOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VOut VSMain(uint id : SV_VertexID) {
    static const float2 positions[6] = {
        float2(-0.5,  0.5), float2( 0.5,  0.5), float2(-0.5, -0.5),
        float2(-0.5, -0.5), float2( 0.5,  0.5), float2( 0.5, -0.5)
    };
    static const float2 texcoords[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1)
    };
    VOut o;
    o.position = mul(float4(positions[id], 0, 1), worldViewProjection);
    o.uv = uvScaleOffset.zw + texcoords[id] * uvScaleOffset.xy;
    return o;
}
float4 PSMain(VOut i) : SV_Target { return sourceTexture.Sample(linearClamp, i.uv); }
)";
        ComPtr<ID3DBlob> vertexBlob, pixelBlob, errors;
        HRESULT hr = D3DCompile(shader, sizeof(shader) - 1, "capture-blit", nullptr, nullptr,
                                "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                &vertexBlob, &errors);
        if (FAILED(hr)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "VS compile failed");
        errors.Reset();
        hr = D3DCompile(shader, sizeof(shader) - 1, "capture-blit", nullptr, nullptr,
                        "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                        &pixelBlob, &errors);
        if (FAILED(hr)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "PS compile failed");
        checkHr(device->CreateVertexShader(vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &vs), "CreateVertexShader");
        checkHr(device->CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, &ps), "CreatePixelShader");

        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        checkHr(device->CreateSamplerState(&samplerDesc, &sampler), "CreateSamplerState");

        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.ByteWidth = 80;
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        checkHr(device->CreateBuffer(&bufferDesc, nullptr, &constants), "CreateBuffer");
    }

    void drawTexture(ID3D11Device* device, ID3D11DeviceContext* context,
                     ID3D11ShaderResourceView* source, ID3D11Texture2D* destination,
                     uint32_t arraySlice, DXGI_FORMAT renderTargetFormat,
                     uint32_t width, uint32_t height,
                     const DirectX::XMFLOAT4X4& worldViewProjection,
                     const std::array<float, 4>& uvScaleOffset) {
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
        D3D11_TEXTURE2D_DESC destinationDesc{};
        destination->GetDesc(&destinationDesc);
        // OpenXR runtimes commonly allocate a typeless resource even when the
        // application requested a typed swapchain format. D3D11 rejects a
        // typeless RTV, so use the typed format negotiated with OpenXR.
        rtvDesc.Format = renderTargetFormat;
        if (destinationDesc.ArraySize == 1) {
            if (arraySlice != 0) throw std::runtime_error("single-slice OpenXR texture requested with non-zero RTV slice");
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
        } else {
            if (arraySlice >= destinationDesc.ArraySize) throw std::runtime_error("OpenXR RTV slice exceeds actual texture array size");
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.MipSlice = 0;
            rtvDesc.Texture2DArray.FirstArraySlice = arraySlice;
            rtvDesc.Texture2DArray.ArraySize = 1;
        }
        ComPtr<ID3D11RenderTargetView> rtv;
        checkHr(device->CreateRenderTargetView(destination, &rtvDesc, &rtv), "CreateRenderTargetView");

        D3D11_MAPPED_SUBRESOURCE mapped{};
        checkHr(context->Map(constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map constants");
        std::memcpy(mapped.pData, &worldViewProjection, sizeof(worldViewProjection));
        auto* values = reinterpret_cast<float*>(static_cast<uint8_t*>(mapped.pData) + 64);
        std::memcpy(values, uvScaleOffset.data(), sizeof(float) * uvScaleOffset.size());
        context->Unmap(constants.Get(), 0);

        const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        ID3D11RenderTargetView* rawRtv = rtv.Get();
        ID3D11ShaderResourceView* rawSrv = source;
        ID3D11SamplerState* rawSampler = sampler.Get();
        ID3D11Buffer* rawConstants = constants.Get();
        context->OMSetRenderTargets(1, &rawRtv, nullptr);
        constexpr float clear[4] = {0, 0, 0, 1};
        context->ClearRenderTargetView(rawRtv, clear);
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &rawConstants);
        context->PSSetShader(ps.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &rawSrv);
        context->PSSetSamplers(0, 1, &rawSampler);
        context->Draw(6, 0);
        rawSrv = nullptr;
        context->PSSetShaderResources(0, 1, &rawSrv);
        rawRtv = nullptr;
        context->OMSetRenderTargets(1, &rawRtv, nullptr);
        context->Flush();
    }

    void draw(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* source,
              const CaptureSource& capture, ID3D11Texture2D* destination, uint32_t arraySlice,
              DXGI_FORMAT renderTargetFormat, uint32_t width, uint32_t height,
              const DirectX::XMFLOAT4X4& worldViewProjection) {
        const float desktopW = static_cast<float>(capture.sourceWidth);
        const float desktopH = static_cast<float>(capture.sourceHeight);
        const std::array<float, 4> uv{
            static_cast<float>(capture.crop.right - capture.crop.left) / desktopW,
            static_cast<float>(capture.crop.bottom - capture.crop.top) / desktopH,
            static_cast<float>(capture.crop.left) / desktopW,
            static_cast<float>(capture.crop.top) / desktopH};
        drawTexture(device, context, source, destination, arraySlice, renderTargetFormat,
                    width, height, worldViewProjection, uv);
    }

    void drawFullscreen(ID3D11Device* device, ID3D11DeviceContext* context,
                        ID3D11ShaderResourceView* source, ID3D11Texture2D* destination,
                        DXGI_FORMAT renderTargetFormat, uint32_t width, uint32_t height,
                        const std::array<float, 4>& uvScaleOffset) {
        DirectX::XMFLOAT4X4 fullscreen{};
        fullscreen._11 = 2.0f;
        fullscreen._22 = 2.0f;
        fullscreen._33 = 1.0f;
        fullscreen._44 = 1.0f;
        drawTexture(device, context, source, destination, 0, renderTargetFormat,
                    width, height, fullscreen, uvScaleOffset);
    }
};

void probeRenderedEye(ID3D11Device* device, ID3D11DeviceContext* context,
                      ID3D11Texture2D* renderedEye, const std::filesystem::path& screenshotPath,
                      Logger& log) {
    D3D11_TEXTURE2D_DESC desc{};
    renderedEye->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.ArraySize = 1;
    stagingDesc.MipLevels = 1;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.SampleDesc.Quality = 0;
    ComPtr<ID3D11Texture2D> staging;
    checkHr(device->CreateTexture2D(&stagingDesc, nullptr, &staging), "Create render-probe staging texture");
    context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, renderedEye, 0, nullptr);
    context->Flush();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    checkHr(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map render-probe staging texture");
    uint32_t lumaMin = 255;
    uint32_t lumaMax = 0;
    uint64_t nonBlack = 0;
    for (uint32_t y = 0; y < desc.Height; ++y) {
        const auto* row = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        for (uint32_t x = 0; x < desc.Width; ++x) {
            const auto* pixel = row + static_cast<size_t>(x) * 4;
            const uint32_t luma = (static_cast<uint32_t>(pixel[0]) + pixel[1] + pixel[2]) / 3;
            lumaMin = std::min(lumaMin, luma);
            lumaMax = std::max(lumaMax, luma);
            if (pixel[0] > 4 || pixel[1] > 4 || pixel[2] > 4) ++nonBlack;
        }
    }
    const uint64_t pixels = static_cast<uint64_t>(desc.Width) * desc.Height;
    log.line("render_probe_eye=0 width=", desc.Width, " height=", desc.Height,
             " texture_format=", static_cast<uint32_t>(desc.Format),
             " luma_min=", lumaMin, " luma_max=", lumaMax,
             " nonblack=", nonBlack, " pixels=", pixels,
             " nonblack_percent=", std::fixed, std::setprecision(3),
             pixels ? (100.0 * static_cast<double>(nonBlack) / static_cast<double>(pixels)) : 0.0);

    if (!screenshotPath.empty()) {
        if (screenshotPath.has_parent_path()) std::filesystem::create_directories(screenshotPath.parent_path());
        std::ofstream output(screenshotPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            context->Unmap(staging.Get(), 0);
            throw std::runtime_error("failed to open --screenshot output");
        }
        BITMAPFILEHEADER fileHeader{};
        BITMAPINFOHEADER infoHeader{};
        const uint32_t imageBytes = desc.Width * desc.Height * 4;
        fileHeader.bfType = 0x4D42;
        fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
        fileHeader.bfSize = fileHeader.bfOffBits + imageBytes;
        infoHeader.biSize = sizeof(infoHeader);
        infoHeader.biWidth = static_cast<LONG>(desc.Width);
        infoHeader.biHeight = -static_cast<LONG>(desc.Height);
        infoHeader.biPlanes = 1;
        infoHeader.biBitCount = 32;
        infoHeader.biCompression = BI_RGB;
        infoHeader.biSizeImage = imageBytes;
        output.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
        output.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));
        const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ||
                          desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                          desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        std::vector<uint8_t> converted;
        if (!bgra) converted.resize(static_cast<size_t>(desc.Width) * 4);
        for (uint32_t y = 0; y < desc.Height; ++y) {
            const auto* row = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
            if (bgra) {
                output.write(reinterpret_cast<const char*>(row), static_cast<std::streamsize>(desc.Width) * 4);
            } else {
                for (uint32_t x = 0; x < desc.Width; ++x) {
                    converted[x * 4 + 0] = row[x * 4 + 2];
                    converted[x * 4 + 1] = row[x * 4 + 1];
                    converted[x * 4 + 2] = row[x * 4 + 0];
                    converted[x * 4 + 3] = row[x * 4 + 3];
                }
                output.write(reinterpret_cast<const char*>(converted.data()),
                             static_cast<std::streamsize>(converted.size()));
            }
        }
        if (!output) {
            context->Unmap(staging.Get(), 0);
            throw std::runtime_error("failed while writing --screenshot BMP");
        }
        log.line("render_probe_screenshot=", screenshotPath.string(), " bytes=", fileHeader.bfSize);
    }
    context->Unmap(staging.Get(), 0);
}

XrQuaternionf multiply(const XrQuaternionf& a, const XrQuaternionf& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

XrVector3f rotate(const XrQuaternionf& q, const XrVector3f& v) {
    const XrQuaternionf p{v.x, v.y, v.z, 0};
    const XrQuaternionf inverse{-q.x, -q.y, -q.z, q.w};
    const auto r = multiply(multiply(q, p), inverse);
    return {r.x, r.y, r.z};
}

XrQuaternionf normalized(const XrQuaternionf& q) {
    const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length < 1.0e-6f) return {0, 0, 0, 1};
    return {q.x / length, q.y / length, q.z / length, q.w / length};
}

DirectX::XMFLOAT4X4 planeWorldViewProjection(const XrPosef& planePose, float planeWidth,
                                             float planeHeight, const XrView& eye) {
    using namespace DirectX;
    const XMVECTOR planeQ = XMVectorSet(planePose.orientation.x, planePose.orientation.y,
                                        planePose.orientation.z, planePose.orientation.w);
    const XMMATRIX world = XMMatrixScaling(planeWidth, planeHeight, 1.0f) *
                           XMMatrixRotationQuaternion(planeQ) *
                           XMMatrixTranslation(planePose.position.x, planePose.position.y, planePose.position.z);
    const XMVECTOR eyeQ = XMVectorSet(eye.pose.orientation.x, eye.pose.orientation.y,
                                      eye.pose.orientation.z, eye.pose.orientation.w);
    const XMMATRIX eyeWorld = XMMatrixRotationQuaternion(eyeQ) *
                              XMMatrixTranslation(eye.pose.position.x, eye.pose.position.y, eye.pose.position.z);
    const XMMATRIX view = XMMatrixInverse(nullptr, eyeWorld);
    constexpr float nearZ = 0.05f;
    constexpr float farZ = 100.0f;
    const XMMATRIX projection = XMMatrixPerspectiveOffCenterRH(
        std::tan(eye.fov.angleLeft) * nearZ, std::tan(eye.fov.angleRight) * nearZ,
        std::tan(eye.fov.angleDown) * nearZ, std::tan(eye.fov.angleUp) * nearZ, nearZ, farZ);
    XMFLOAT4X4 result{};
    XMStoreFloat4x4(&result, world * view * projection);
    return result;
}

std::string sessionStateName(XrSessionState state) {
    switch (state) {
    case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
    case XR_SESSION_STATE_IDLE: return "IDLE";
    case XR_SESSION_STATE_READY: return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
    case XR_SESSION_STATE_STOPPING: return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING: return "EXITING";
    default: return std::to_string(state);
    }
}

struct GazeProjectionTelemetry final {
    EyeGazeSample sample{};
    std::array<anvilvr::shadows_eye_gaze::Projection, 2> eye{};
    bool usedHeadFallback = true;
};

GazeProjectionTelemetry projectGazeToSourceEyes(
    const EyeGazeSample& sample,
    const anvilvr::shadows_native_color::EyeFrameMetadata& left,
    const anvilvr::shadows_native_color::EyeFrameMetadata& right) {
    using namespace anvilvr::shadows_eye_gaze;
    GazeProjectionTelemetry result;
    result.sample = sample;
    result.usedHeadFallback = !sample.actualValid;
    const Quaternion gaze = sample.actualValid
        ? Quaternion{sample.orientation.x, sample.orientation.y,
                     sample.orientation.z, sample.orientation.w}
        : Quaternion{left.orientation[0], left.orientation[1],
                     left.orientation[2], left.orientation[3]};
    const std::array<const anvilvr::shadows_native_color::EyeFrameMetadata*, 2> metadata{&left, &right};
    for (std::size_t eye = 0; eye < metadata.size(); ++eye) {
        const auto& source = *metadata[eye];
        const Pose pose{
            {source.orientation[0], source.orientation[1], source.orientation[2], source.orientation[3]},
            {source.position[0], source.position[1], source.position[2]}};
        const Fov fov{source.fov[0], source.fov[1], source.fov[2], source.fov[3]};
        result.eye[eye] = project(gaze, pose, fov);
    }
    return result;
}

int run(const Options& options, Logger& log, StopControl& stopControl) {
    anvilvr::shadows_pose::Publisher posePublisher;
    log.line("pose_transport mapping=Local\\AnvilShadowsVR.Pose.v1 version=1",
             " semantic=openxr_local_space_per_eye_pose_only");
    uint32_t extensionCount = 0;
    checkXr(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr), "xrEnumerateInstanceExtensionProperties");
    std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
    checkXr(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data()), "xrEnumerateInstanceExtensionProperties");
    const bool hasD3D11 = std::any_of(extensions.begin(), extensions.end(), [](const auto& e) {
        return std::string(e.extensionName) == XR_KHR_D3D11_ENABLE_EXTENSION_NAME;
    });
    const bool hasEyeGazeExtension = std::any_of(extensions.begin(), extensions.end(), [](const auto& e) {
        return std::string(e.extensionName) == XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME;
    });
    if (!hasD3D11) throw std::runtime_error("active OpenXR runtime does not advertise XR_KHR_D3D11_enable");

    XrHandles xr;
    std::vector<const char*> enabledExtensions{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    if (hasEyeGazeExtension)
        enabledExtensions.push_back(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
    XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    std::snprintf(instanceInfo.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "%s", "Shadows VR Screen");
    instanceInfo.applicationInfo.applicationVersion = 1;
    std::snprintf(instanceInfo.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "%s", "AnvilVR screen bridge");
    instanceInfo.applicationInfo.engineVersion = 1;
    instanceInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size());
    instanceInfo.enabledExtensionNames = enabledExtensions.data();
    checkXr(xrCreateInstance(&instanceInfo, &xr.instance), "xrCreateInstance");

    XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
    checkXr(xrGetInstanceProperties(xr.instance, &instanceProperties), "xrGetInstanceProperties");
    log.line("runtime=", instanceProperties.runtimeName, " version=",
             XR_VERSION_MAJOR(instanceProperties.runtimeVersion), ".",
             XR_VERSION_MINOR(instanceProperties.runtimeVersion), ".",
             XR_VERSION_PATCH(instanceProperties.runtimeVersion));

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId systemId{};
    checkXr(xrGetSystem(xr.instance, &systemInfo, &systemId), "xrGetSystem (headset/runtime must be ready)");

    XrSystemEyeGazeInteractionPropertiesEXT eyeGazeProperties{
        XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT};
    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
    systemProperties.next = hasEyeGazeExtension ? &eyeGazeProperties : nullptr;
    checkXr(xrGetSystemProperties(xr.instance, systemId, &systemProperties), "xrGetSystemProperties");
    const bool supportsEyeGaze = hasEyeGazeExtension && eyeGazeProperties.supportsEyeGazeInteraction == XR_TRUE;
    log.line("system=", systemProperties.systemName, " max_swapchain=",
             systemProperties.graphicsProperties.maxSwapchainImageWidth, "x",
             systemProperties.graphicsProperties.maxSwapchainImageHeight);
    log.line("eye_gaze_extension_advertised=", hasEyeGazeExtension ? "true" : "false",
             " supports_eye_gaze_interaction=", supportsEyeGaze ? "true" : "false");

    PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;
    checkXr(xrGetInstanceProcAddr(xr.instance, "xrGetD3D11GraphicsRequirementsKHR",
                                  reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements)),
            "xrGetInstanceProcAddr(xrGetD3D11GraphicsRequirementsKHR)");
    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    checkXr(getRequirements(xr.instance, systemId, &requirements), "xrGetD3D11GraphicsRequirementsKHR");
    D3DContext d3d = createDeviceForLuid(requirements.adapterLuid, requirements.minFeatureLevel);
    ComPtr<ID3D11Multithread> d3dMultithread;
    checkHr(d3d.context.As(&d3dMultithread), "Query ID3D11Multithread");
    const BOOL multithreadWasProtected = d3dMultithread->SetMultithreadProtected(TRUE);
    const std::string adapterName = utf8(d3d.adapterDesc.Description);
    log.line("adapter=", adapterName, " luid=", luidString(d3d.adapterDesc.AdapterLuid),
             " vendor=0x", std::hex, d3d.adapterDesc.VendorId, std::dec);
    log.line("d3d11_multithread_protected=true previous=", multithreadWasProtected ? "true" : "false");

    if (options.probeOnly) {
        log.line("probe_only=true result=ready");
        return 0;
    }

    const TargetWindow target = waitForGameWindow(options, log, stopControl.handle());
    if (target.cancelled) {
        log.line("result=stopped_before_session");
        return 0;
    }
    const DWORD pid = target.pid;
    const HWND hwnd = target.hwnd;
    const RECT initialBounds = windowBounds(hwnd);
    log.line("target_pid=", pid, " target_hwnd=0x", std::hex,
             reinterpret_cast<uintptr_t>(hwnd), std::dec, " bounds=",
             initialBounds.left, ",", initialBounds.top, "-", initialBounds.right, ",", initialBounds.bottom);

    std::unique_ptr<CaptureSource> capture;
    std::unique_ptr<NativeStereoConsumer> nativeConsumer;
    if (options.nativeStereo) {
        nativeConsumer = std::make_unique<NativeStereoConsumer>(
            options.nativeColorHelper, options.nativeColorMapping,
            d3d.device.Get(), d3d.context.Get(), log);
    } else {
        try {
            auto wgc = std::make_unique<WindowGraphicsCapture>();
            wgc->initialize(d3d.device.Get(), hwnd);
            capture = std::move(wgc);
        } catch (const std::exception& e) {
            log.line("capture_primary_failed=windows_graphics_capture_hwnd reason=", e.what(),
                     " fallback=desktop_duplication_fallback");
            auto desktop = std::make_unique<DesktopCapture>();
            desktop->initialize(d3d.device.Get(), d3d.adapter.Get(), hwnd);
            capture = std::move(desktop);
        }
    }
    uint32_t viewCount = 0;
    checkXr(xrEnumerateViewConfigurationViews(xr.instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                               0, &viewCount, nullptr),
            "xrEnumerateViewConfigurationViews");
    if (viewCount != 2) throw std::runtime_error("PRIMARY_STEREO did not expose exactly two views");
    std::vector<XrViewConfigurationView> viewConfigs(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    checkXr(xrEnumerateViewConfigurationViews(xr.instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                               viewCount, &viewCount, viewConfigs.data()),
            "xrEnumerateViewConfigurationViews");
    const uint32_t swapchainWidth = std::min(viewConfigs[0].recommendedImageRectWidth,
        systemProperties.graphicsProperties.maxSwapchainImageWidth);
    const uint32_t swapchainHeight = std::min(viewConfigs[0].recommendedImageRectHeight,
        systemProperties.graphicsProperties.maxSwapchainImageHeight);
    if (options.nativeStereo) {
        log.line("capture_source=native_stereo_shared_textures semantic=independent_game_eye_images",
                 " mono_fallback=false swapchain=", swapchainWidth, "x", swapchainHeight);
    } else {
        log.line("capture_source=", capture->name(), " capture_output=", capture->sourceWidth, "x", capture->sourceHeight,
                 " crop=", capture->crop.left, ",", capture->crop.top, "-", capture->crop.right, ",", capture->crop.bottom,
                 " swapchain=", swapchainWidth, "x", swapchainHeight);
    }

    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = d3d.device.Get();
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &binding;
    sessionInfo.systemId = systemId;
    checkXr(xrCreateSession(xr.instance, &sessionInfo, &xr.session), "xrCreateSession");

    const XrPosef identity{{0, 0, 0, 1}, {0, 0, 0}};
    XrReferenceSpaceCreateInfo localInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    localInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    localInfo.poseInReferenceSpace = identity;
    checkXr(xrCreateReferenceSpace(xr.session, &localInfo, &xr.localSpace), "xrCreateReferenceSpace(LOCAL)");
    XrReferenceSpaceCreateInfo viewInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    viewInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    viewInfo.poseInReferenceSpace = identity;
    checkXr(xrCreateReferenceSpace(xr.session, &viewInfo, &xr.viewSpace), "xrCreateReferenceSpace(VIEW)");

    EyeGazeTracker eyeGazeTracker;
    eyeGazeTracker.initialize(xr.instance, xr.session, xr.localSpace,
                              hasEyeGazeExtension, supportsEyeGaze, log);

    uint32_t formatCount = 0;
    checkXr(xrEnumerateSwapchainFormats(xr.session, 0, &formatCount, nullptr), "xrEnumerateSwapchainFormats");
    std::vector<int64_t> formats(formatCount);
    checkXr(xrEnumerateSwapchainFormats(xr.session, formatCount, &formatCount, formats.data()), "xrEnumerateSwapchainFormats");
    constexpr DXGI_FORMAT preferences[] = {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM,
                                           DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
    DXGI_FORMAT swapchainFormat = DXGI_FORMAT_UNKNOWN;
    for (const auto wanted : preferences) {
        if (std::find(formats.begin(), formats.end(), static_cast<int64_t>(wanted)) != formats.end()) {
            swapchainFormat = wanted;
            break;
        }
    }
    if (swapchainFormat == DXGI_FORMAT_UNKNOWN) throw std::runtime_error("runtime exposes no supported 8-bit BGRA/RGBA swapchain format");

    XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.format = static_cast<int64_t>(swapchainFormat);
    swapchainInfo.sampleCount = 1;
    swapchainInfo.width = swapchainWidth;
    swapchainInfo.height = swapchainHeight;
    swapchainInfo.faceCount = 1;
    // Use one swapchain per eye. Some runtimes accept arraySize=2 but expose
    // D3D11 textures with ArraySize=1, making slice-1 RTV creation invalid.
    swapchainInfo.arraySize = 1;
    swapchainInfo.mipCount = 1;
    std::array<std::vector<XrSwapchainImageD3D11KHR>, 2> images;
    for (uint32_t eye = 0; eye < viewCount; ++eye) {
        checkXr(xrCreateSwapchain(xr.session, &swapchainInfo, &xr.swapchains[eye]), "xrCreateSwapchain(per-eye)");
        uint32_t imageCount = 0;
        checkXr(xrEnumerateSwapchainImages(xr.swapchains[eye], 0, &imageCount, nullptr),
                "xrEnumerateSwapchainImages(count)");
        images[eye].assign(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        checkXr(xrEnumerateSwapchainImages(xr.swapchains[eye], imageCount, &imageCount,
                                          reinterpret_cast<XrSwapchainImageBaseHeader*>(images[eye].data())),
                "xrEnumerateSwapchainImages(data)");
        if (images[eye].empty() || !images[eye][0].texture)
            throw std::runtime_error("runtime returned an empty D3D11 swapchain");
        D3D11_TEXTURE2D_DESC actual{};
        images[eye][0].texture->GetDesc(&actual);
        log.line("swapchain_eye=", eye, " requested=", swapchainWidth, "x", swapchainHeight,
                 " requested_array=1 actual=", actual.Width, "x", actual.Height,
                 " actual_format=", static_cast<uint32_t>(actual.Format),
                 " actual_array=", actual.ArraySize, " actual_samples=", actual.SampleDesc.Count,
                 " actual_bind=0x", std::hex, actual.BindFlags, std::dec,
                 " images=", imageCount);
        if ((actual.BindFlags & D3D11_BIND_RENDER_TARGET) == 0)
            throw std::runtime_error("runtime swapchain texture lacks D3D11_BIND_RENDER_TARGET");
    }
    log.line("swapchain_format=", static_cast<int>(swapchainFormat), " layout=independent_per_eye");

    Blitter blitter;
    blitter.initialize(d3d.device.Get());

    bool sessionRunning = false;
    bool exitRequested = false;
    bool quadAnchored = false;
    bool renderProbed = false;
    bool stoppedByControl = false;
    bool gameWindowDestroyed = false;
    XrSessionState currentSessionState = XR_SESSION_STATE_UNKNOWN;
    std::optional<std::chrono::steady_clock::time_point> focusedSince;
    XrPosef quadPose = identity;
    uint64_t submitted = 0;
    GazeProjectionTelemetry gazeTelemetry{};
    std::uint64_t actualGazeSamples = 0;
    std::uint64_t headFallbackSamples = 0;
    auto started = std::chrono::steady_clock::now();
    auto lastStatus = started;

    while (!exitRequested &&
           (options.durationSeconds == 0.0 ||
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() < options.durationSeconds)) {
        if (stopControl.requested()) {
            stoppedByControl = true;
            log.line("stop_event_observed=true");
            break;
        }
        if (!IsWindow(hwnd)) {
            gameWindowDestroyed = true;
            log.line("game_window_destroyed=true target_hwnd=0x", std::hex,
                     reinterpret_cast<uintptr_t>(hwnd), std::dec);
            break;
        }
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(xr.instance, &event) == XR_SUCCESS) {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                currentSessionState = changed->state;
                log.line("session_state=", sessionStateName(changed->state));
                if (changed->state == XR_SESSION_STATE_FOCUSED && !focusedSince)
                    focusedSince = std::chrono::steady_clock::now();
                else if (changed->state != XR_SESSION_STATE_FOCUSED && !quadAnchored)
                    focusedSince.reset();
                if (changed->state == XR_SESSION_STATE_READY && !sessionRunning) {
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                    begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    checkXr(xrBeginSession(xr.session, &begin), "xrBeginSession");
                    sessionRunning = true;
                } else if (changed->state == XR_SESSION_STATE_STOPPING && sessionRunning) {
                    checkXr(xrEndSession(xr.session), "xrEndSession");
                    sessionRunning = false;
                } else if (changed->state == XR_SESSION_STATE_EXITING || changed->state == XR_SESSION_STATE_LOSS_PENDING) {
                    exitRequested = true;
                }
            } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                exitRequested = true;
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (!sessionRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        checkXr(xrWaitFrame(xr.session, &waitInfo, &frameState), "xrWaitFrame");
        XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
        checkXr(xrBeginFrame(xr.session, &beginInfo), "xrBeginFrame");

        if (capture) capture->acquire(d3d.context.Get(), hwnd);
        std::vector<const XrCompositionLayerBaseHeader*> layers;
        XrCompositionLayerProjection projectionLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        std::vector<XrCompositionLayerProjectionView> projectionViews(viewCount,
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});
        std::vector<XrView> eyeViews(viewCount, {XR_TYPE_VIEW});
        if (frameState.shouldRender && (options.nativeStereo || capture->frames > 0)) {
            if (options.nativeStereo && !quadAnchored) {
                quadAnchored = true;
                log.line("projection_mode=native_stereo semantic=game_rendered_per_eye_texture",
                         " plane_projection=false mono_fallback=false");
            }
            if (options.headLocked && !quadAnchored) {
                quadAnchored = true;
                log.line("projection_plane_mode=head_locked semantic=flat_game_capture_always_in_front");
            }
            // KRVR initially serves a synthetic pose, then replaces it when the
            // remote headset pose uplink becomes live. Anchoring before FOCUSED
            // can leave the plane outside the user's actual view after that
            // handoff, so wait briefly for the focused pose stream to settle.
            const bool focusPoseSettled = currentSessionState == XR_SESSION_STATE_FOCUSED &&
                focusedSince && std::chrono::steady_clock::now() - *focusedSince >= std::chrono::milliseconds(750);
            if (!options.headLocked && !quadAnchored && focusPoseSettled) {
                XrViewLocateInfo anchorLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
                anchorLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                anchorLocateInfo.displayTime = frameState.predictedDisplayTime;
                anchorLocateInfo.space = xr.localSpace;
                XrViewState anchorViewState{XR_TYPE_VIEW_STATE};
                uint32_t anchorViewCount = 0;
                checkXr(xrLocateViews(xr.session, &anchorLocateInfo, &anchorViewState, viewCount,
                                      &anchorViewCount, eyeViews.data()), "xrLocateViews(anchor)");
                const XrViewStateFlags anchorValid = XR_VIEW_STATE_POSITION_VALID_BIT |
                                                     XR_VIEW_STATE_ORIENTATION_VALID_BIT;
                if (anchorViewCount == viewCount && (anchorViewState.viewStateFlags & anchorValid) == anchorValid) {
                    quadPose.orientation = eyeViews[0].pose.orientation;
                    quadPose.position = {
                        (eyeViews[0].pose.position.x + eyeViews[1].pose.position.x) * 0.5f,
                        (eyeViews[0].pose.position.y + eyeViews[1].pose.position.y) * 0.5f,
                        (eyeViews[0].pose.position.z + eyeViews[1].pose.position.z) * 0.5f};
                    const auto offset = rotate(quadPose.orientation, {0, 0, -options.distanceMetres});
                    quadPose.position.x += offset.x;
                    quadPose.position.y += offset.y;
                    quadPose.position.z += offset.z;
                    quadAnchored = true;
                    log.line("projection_plane_anchored=true distance_m=", options.distanceMetres,
                             " width_m=", options.widthMetres,
                             " semantic=flat_game_capture_in_world_locked_stereo_projection");
                }
            }
            if (quadAnchored) {
                XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
                locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                locateInfo.displayTime = frameState.predictedDisplayTime;
                locateInfo.space = xr.localSpace;
                XrViewState viewState{XR_TYPE_VIEW_STATE};
                uint32_t locatedViewCount = 0;
                checkXr(xrLocateViews(xr.session, &locateInfo, &viewState, viewCount,
                                      &locatedViewCount, eyeViews.data()), "xrLocateViews");
                if (locatedViewCount == 2) {
                    std::array<anvilvr::shadows_pose::EyePose, 2> publishedEyes{};
                    for (std::size_t eye = 0; eye < publishedEyes.size(); ++eye) {
                        const XrView& source = eyeViews[eye];
                        auto& destination = publishedEyes[eye];
                        destination.orientation[0] = source.pose.orientation.x;
                        destination.orientation[1] = source.pose.orientation.y;
                        destination.orientation[2] = source.pose.orientation.z;
                        destination.orientation[3] = source.pose.orientation.w;
                        destination.position[0] = source.pose.position.x;
                        destination.position[1] = source.pose.position.y;
                        destination.position[2] = source.pose.position.z;
                        destination.fov[0] = source.fov.angleLeft;
                        destination.fov[1] = source.fov.angleRight;
                        destination.fov[2] = source.fov.angleUp;
                        destination.fov[3] = source.fov.angleDown;
                    }
                    std::uint32_t poseValidity = 0;
                    if ((viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0)
                        poseValidity |= anvilvr::shadows_pose::kOrientationValid;
                    if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0)
                        poseValidity |= anvilvr::shadows_pose::kPositionValid;
                    if (!posePublisher.publish(frameState.predictedDisplayTime, poseValidity, publishedEyes))
                        log.line("pose_transport_publish=false reason=mutex_timeout_or_clock_failure");
                }
                const XrViewStateFlags validViews = XR_VIEW_STATE_POSITION_VALID_BIT |
                                                    XR_VIEW_STATE_ORIENTATION_VALID_BIT;
                if (locatedViewCount == viewCount && (viewState.viewStateFlags & validViews) == validViews) {
                    if (options.headLocked) {
                        quadPose.orientation = normalized(eyeViews[0].pose.orientation);
                        quadPose.position = {
                            (eyeViews[0].pose.position.x + eyeViews[1].pose.position.x) * 0.5f,
                            (eyeViews[0].pose.position.y + eyeViews[1].pose.position.y) * 0.5f,
                            (eyeViews[0].pose.position.z + eyeViews[1].pose.position.z) * 0.5f};
                        const auto offset = rotate(quadPose.orientation, {0, 0, -options.distanceMetres});
                        quadPose.position.x += offset.x;
                        quadPose.position.y += offset.y;
                        quadPose.position.z += offset.z;
                    }
                    if (options.nativeStereo) {
                        for (uint32_t eye = 0; eye < viewCount; ++eye)
                            nativeConsumer->acquireLatest(eye);
                        nativeConsumer->commitLatestPair();
                        if (nativeConsumer->ready()) {
                            nativeConsumer->resolveContentCropFromEye0();
                            const auto& leftSource = nativeConsumer->metadata(0);
                            const auto& rightSource = nativeConsumer->metadata(1);
                            const EyeGazeSample gazeSample = eyeGazeTracker.sample(
                                xr.session, static_cast<XrTime>(leftSource.predictedDisplayTime),
                                currentSessionState == XR_SESSION_STATE_FOCUSED);
                            gazeTelemetry = projectGazeToSourceEyes(gazeSample, leftSource, rightSource);
                            if (gazeTelemetry.sample.actualValid) ++actualGazeSamples;
                            else ++headFallbackSamples;
                            for (uint32_t eye = 0; eye < viewCount; ++eye) {
                                uint32_t imageIndex = 0;
                                XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                                checkXr(xrAcquireSwapchainImage(xr.swapchains[eye], &acquireInfo, &imageIndex),
                                        "xrAcquireSwapchainImage(native stereo per-eye)");
                                XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                                imageWait.timeout = XR_INFINITE_DURATION;
                                checkXr(xrWaitSwapchainImage(xr.swapchains[eye], &imageWait),
                                        "xrWaitSwapchainImage(native stereo per-eye)");
                                if (imageIndex >= images[eye].size())
                                    throw std::runtime_error("runtime returned an out-of-range native stereo swapchain image index");
                                if (eye == 0 && !renderProbed) {
                                    ComPtr<ID3D11Resource> sourceResource;
                                    ComPtr<ID3D11Texture2D> sourceTexture;
                                    nativeConsumer->srv(eye)->GetResource(&sourceResource);
                                    checkHr(sourceResource.As(&sourceTexture), "native source texture query");
                                    log.line("native_source_pixel_probe=true");
                                    probeRenderedEye(d3d.device.Get(), d3d.context.Get(), sourceTexture.Get(), {}, log);
                                }
                                blitter.drawFullscreen(d3d.device.Get(), d3d.context.Get(),
                                                       nativeConsumer->srv(eye),
                                                       images[eye][imageIndex].texture,
                                                       swapchainFormat, swapchainWidth, swapchainHeight,
                                                       nativeConsumer->contentUv());
                                if (eye == 0 && !renderProbed) {
                                    probeRenderedEye(d3d.device.Get(), d3d.context.Get(),
                                                     images[eye][imageIndex].texture,
                                                     options.screenshotPath, log);
                                    renderProbed = true;
                                }
                                XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                                checkXr(xrReleaseSwapchainImage(xr.swapchains[eye], &releaseInfo),
                                        "xrReleaseSwapchainImage(native stereo per-eye)");

                                const auto& metadata = nativeConsumer->metadata(eye);
                                projectionViews[eye].pose.orientation = {
                                    metadata.orientation[0], metadata.orientation[1],
                                    metadata.orientation[2], metadata.orientation[3]};
                                projectionViews[eye].pose.position = {
                                    metadata.position[0], metadata.position[1], metadata.position[2]};
                                projectionViews[eye].fov = {
                                    metadata.fov[0], metadata.fov[1], metadata.fov[2], metadata.fov[3]};
                                projectionViews[eye].subImage.swapchain = xr.swapchains[eye];
                                projectionViews[eye].subImage.imageRect = {
                                    {0, 0}, {static_cast<int32_t>(swapchainWidth), static_cast<int32_t>(swapchainHeight)}};
                                projectionViews[eye].subImage.imageArrayIndex = 0;
                            }
                            projectionLayer.space = xr.localSpace;
                            projectionLayer.viewCount = viewCount;
                            projectionLayer.views = projectionViews.data();
                            layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer));
                            ++submitted;
                        }
                    } else {
                        const float planeHeight = options.widthMetres *
                            static_cast<float>(capture->crop.bottom - capture->crop.top) /
                            static_cast<float>(capture->crop.right - capture->crop.left);
                        for (uint32_t eye = 0; eye < viewCount; ++eye) {
                        uint32_t imageIndex = 0;
                        XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                        checkXr(xrAcquireSwapchainImage(xr.swapchains[eye], &acquireInfo, &imageIndex),
                                "xrAcquireSwapchainImage(per-eye)");
                        XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                        imageWait.timeout = XR_INFINITE_DURATION;
                        checkXr(xrWaitSwapchainImage(xr.swapchains[eye], &imageWait),
                                "xrWaitSwapchainImage(per-eye)");
                        if (imageIndex >= images[eye].size())
                            throw std::runtime_error("runtime returned an out-of-range swapchain image index");
                        const auto matrix = planeWorldViewProjection(quadPose, options.widthMetres,
                                                                     planeHeight, eyeViews[eye]);
                        blitter.draw(d3d.device.Get(), d3d.context.Get(), capture->srv.Get(), *capture,
                                     images[eye][imageIndex].texture, 0, swapchainFormat,
                                     swapchainWidth, swapchainHeight, matrix);
                        if (eye == 0 && !renderProbed) {
                            probeRenderedEye(d3d.device.Get(), d3d.context.Get(),
                                             images[eye][imageIndex].texture,
                                             options.screenshotPath, log);
                            renderProbed = true;
                        }
                        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                        checkXr(xrReleaseSwapchainImage(xr.swapchains[eye], &releaseInfo),
                                "xrReleaseSwapchainImage(per-eye)");
                        projectionViews[eye].pose = eyeViews[eye].pose;
                        projectionViews[eye].fov = eyeViews[eye].fov;
                        projectionViews[eye].subImage.swapchain = xr.swapchains[eye];
                        projectionViews[eye].subImage.imageRect = {
                            {0, 0}, {static_cast<int32_t>(swapchainWidth), static_cast<int32_t>(swapchainHeight)}};
                        projectionViews[eye].subImage.imageArrayIndex = 0;
                        }

                        projectionLayer.space = xr.localSpace;
                        projectionLayer.viewCount = viewCount;
                        projectionLayer.views = projectionViews.data();
                        layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer));
                        ++submitted;
                    }
                }
            }
        }

        XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
        endInfo.displayTime = frameState.predictedDisplayTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount = static_cast<uint32_t>(layers.size());
        endInfo.layers = layers.empty() ? nullptr : layers.data();
        checkXr(xrEndFrame(xr.session, &endInfo), "xrEndFrame");

        const auto now = std::chrono::steady_clock::now();
        if (now - lastStatus >= std::chrono::seconds(1)) {
            if (options.nativeStereo) {
                log.line("render_active=", frameState.shouldRender ? "true" : "false",
                         " frames_submitted=", submitted,
                         " eye0_source_frame=", nativeConsumer->sourceFrame(0),
                         " eye1_source_frame=", nativeConsumer->sourceFrame(1),
                         " eye0_pose_sequence=", nativeConsumer->poseSequence(0),
                         " eye1_pose_sequence=", nativeConsumer->poseSequence(1),
                         " eye0_fresh=", nativeConsumer->fresh(0),
                         " eye1_fresh=", nativeConsumer->fresh(1),
                         " eye0_stale=", nativeConsumer->stale(0),
                         " eye1_stale=", nativeConsumer->stale(1),
                         " eye0_no_frame=", nativeConsumer->noFrame(0),
                         " eye1_no_frame=", nativeConsumer->noFrame(1),
                         " eye0_errors=", nativeConsumer->errors(0),
                         " eye1_errors=", nativeConsumer->errors(1),
                         " eye0_last_error=", nativeConsumer->lastError(0),
                         " eye1_last_error=", nativeConsumer->lastError(1));
                log.line("native_pair_commits=", nativeConsumer->pairedCommits(),
                         " native_pair_waits=", nativeConsumer->pairWaits(),
                         " native_current_pair=", nativeConsumer->currentPair(),
                         " native_current_pair_explicit=",
                         nativeConsumer->currentPairExplicit() ? "true" : "false");
                log.line("native_generation_changes=", nativeConsumer->generationChanges(),
                         " native_recovery_attempts=", nativeConsumer->recoveryAttempts(),
                         " native_recovery_successes=", nativeConsumer->recoverySuccesses(),
                         " native_recovery_active=",
                         nativeConsumer->recoveryActive() ? "true" : "false");
                log.line("eye_gaze_actual_valid=", gazeTelemetry.sample.actualValid ? "true" : "false",
                         " action_active=", gazeTelemetry.sample.actionActive ? "true" : "false",
                         " orientation_tracked=", gazeTelemetry.sample.orientationTracked ? "true" : "false",
                         " location_flags=0x", std::hex, gazeTelemetry.sample.locationFlags, std::dec,
                         " gaze_source=",
                         gazeTelemetry.usedHeadFallback ? "head_forward_fallback_not_eye_gaze" : "actual_eye_gaze",
                         " requested_source_time=", gazeTelemetry.sample.requestedTime,
                         " gaze_sample_time=", gazeTelemetry.sample.sampleTime,
                         " gaze_sample_age_ns=", gazeTelemetry.sample.sampleAge,
                         " xr_result=", gazeTelemetry.sample.lastResult,
                         " eye0_uv=", gazeTelemetry.eye[0].u, ",", gazeTelemetry.eye[0].v,
                         " eye0_in_frustum=", gazeTelemetry.eye[0].inFrustum ? "true" : "false",
                         " eye1_uv=", gazeTelemetry.eye[1].u, ",", gazeTelemetry.eye[1].v,
                         " eye1_in_frustum=", gazeTelemetry.eye[1].inFrustum ? "true" : "false",
                         " actual_samples=", actualGazeSamples,
                         " head_fallback_samples=", headFallbackSamples,
                         " vrs_active=false");
            } else {
                log.line("render_active=", frameState.shouldRender ? "true" : "false",
                         " frame_capture=", capture->frames, " frames_submitted=", submitted);
            }
            lastStatus = now;
        }
    }

    if (sessionRunning) {
        const XrResult requestResult = xrRequestExitSession(xr.session);
        log.line("session_exit_requested=", XR_SUCCEEDED(requestResult) ? "true" : "false",
                 " result=", requestResult);
        if (XR_SUCCEEDED(requestResult)) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (sessionRunning && std::chrono::steady_clock::now() < deadline) {
                XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
                if (xrPollEvent(xr.instance, &event) == XR_SUCCESS &&
                    event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                    const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                    log.line("session_state=", sessionStateName(changed->state));
                    if (changed->state == XR_SESSION_STATE_STOPPING) {
                        checkXr(xrEndSession(xr.session), "xrEndSession");
                        sessionRunning = false;
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        }
    }

    log.line("result=", stoppedByControl ? "stopped_by_control" :
                         (gameWindowDestroyed ? "stopped_game_window_destroyed" : "completed"),
             " frame_capture=", capture ? capture->frames : 0, " frames_submitted=", submitted,
             " render_active=", submitted > 0 ? "true" : "false");
    if (submitted == 0 && !stoppedByControl && !gameWindowDestroyed)
        throw std::runtime_error("no frame reached the headset; runtime/session never became render-active or capture produced no frame");
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const Options options = parseOptions(argc, argv);
        Logger log(options.logPath);
        StopControl stopControl;
        log.line("stop_event_name=", utf8(stopControl.name()));
        if (options.controlSelfTest) {
            const bool passed = stopControl.selfTest(log);
            log.line("result=control_self_test_", passed ? "passed" : "failed");
            return passed ? 0 : 1;
        }
        return run(options, log, stopControl);
    } catch (const std::exception& e) {
        std::cerr << "fatal=" << e.what() << '\n';
        return 1;
    }
}
