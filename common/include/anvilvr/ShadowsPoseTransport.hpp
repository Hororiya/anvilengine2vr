#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace anvilvr::shadows_pose {

inline constexpr wchar_t kMappingName[] = L"Local\\AnvilShadowsVR.Pose.v1";
inline constexpr wchar_t kMutexName[] = L"Local\\AnvilShadowsVR.PoseMutex.v1";
inline constexpr std::uint32_t kMagic = 0x53505641u; // "AVPS" in little endian.
inline constexpr std::uint16_t kVersion = 1;
inline constexpr std::uint32_t kOrientationValid = 1u << 0;
inline constexpr std::uint32_t kPositionValid = 1u << 1;

struct EyePose final {
    float orientation[4]{}; // x, y, z, w
    float position[3]{};    // metres in the publisher's OpenXR local space
    float fov[4]{};         // angleLeft, angleRight, angleUp, angleDown (radians)
};

struct Snapshot final {
    std::uint32_t magic = kMagic;
    std::uint16_t version = kVersion;
    std::uint16_t structSize = sizeof(Snapshot);
    std::uint64_t sequence = 0;
    std::int64_t predictedDisplayTime = 0; // XrTime, opaque runtime time domain.
    std::int64_t hostQpc = 0;              // QueryPerformanceCounter at publication.
    std::int64_t hostQpcFrequency = 0;
    std::uint32_t viewCount = 0;
    std::uint32_t validityFlags = 0;
    EyePose eyes[2]{};
};

static_assert(sizeof(EyePose) == 44);
static_assert(std::is_standard_layout_v<EyePose> && std::is_trivially_copyable_v<EyePose>);
static_assert(offsetof(EyePose, orientation) == 0);
static_assert(offsetof(EyePose, position) == 16);
static_assert(offsetof(EyePose, fov) == 28);
static_assert(sizeof(Snapshot) == 136);
static_assert(alignof(Snapshot) == 8);
static_assert(std::is_standard_layout_v<Snapshot> && std::is_trivially_copyable_v<Snapshot>);
static_assert(offsetof(Snapshot, magic) == 0);
static_assert(offsetof(Snapshot, version) == 4);
static_assert(offsetof(Snapshot, structSize) == 6);
static_assert(offsetof(Snapshot, sequence) == 8);
static_assert(offsetof(Snapshot, predictedDisplayTime) == 16);
static_assert(offsetof(Snapshot, hostQpc) == 24);
static_assert(offsetof(Snapshot, hostQpcFrequency) == 32);
static_assert(offsetof(Snapshot, viewCount) == 40);
static_assert(offsetof(Snapshot, validityFlags) == 44);
static_assert(offsetof(Snapshot, eyes) == 48);

enum class ValidationResult {
    valid,
    badMagic,
    badVersion,
    badSize,
    unpublished,
    badViewCount,
    missingValidity,
    badClock,
    fromFuture,
    stale,
    nonFinite,
};

inline const char* validationName(ValidationResult value) noexcept {
    switch (value) {
    case ValidationResult::valid: return "valid";
    case ValidationResult::badMagic: return "bad_magic";
    case ValidationResult::badVersion: return "bad_version";
    case ValidationResult::badSize: return "bad_size";
    case ValidationResult::unpublished: return "unpublished";
    case ValidationResult::badViewCount: return "bad_view_count";
    case ValidationResult::missingValidity: return "missing_validity";
    case ValidationResult::badClock: return "bad_clock";
    case ValidationResult::fromFuture: return "from_future";
    case ValidationResult::stale: return "stale";
    case ValidationResult::nonFinite: return "non_finite";
    }
    return "unknown";
}

inline bool finiteEye(const EyePose& eye, std::uint32_t flags) noexcept {
    if ((flags & kOrientationValid) != 0) {
        for (const float value : eye.orientation)
            if (!std::isfinite(value)) return false;
    }
    if ((flags & kPositionValid) != 0) {
        for (const float value : eye.position)
            if (!std::isfinite(value)) return false;
    }
    for (const float value : eye.fov)
        if (!std::isfinite(value)) return false;
    return true;
}

inline ValidationResult validateSnapshot(const Snapshot& snapshot, std::int64_t nowQpc,
                                         std::int64_t maxAgeQpc,
                                         std::uint32_t requiredFlags =
                                             kOrientationValid | kPositionValid) noexcept {
    if (snapshot.magic != kMagic) return ValidationResult::badMagic;
    if (snapshot.version != kVersion) return ValidationResult::badVersion;
    if (snapshot.structSize != sizeof(Snapshot)) return ValidationResult::badSize;
    if (snapshot.sequence == 0) return ValidationResult::unpublished;
    if (snapshot.viewCount != 2) return ValidationResult::badViewCount;
    if ((snapshot.validityFlags & requiredFlags) != requiredFlags)
        return ValidationResult::missingValidity;
    if (snapshot.hostQpc <= 0 || snapshot.hostQpcFrequency <= 0 || nowQpc <= 0 || maxAgeQpc < 0)
        return ValidationResult::badClock;
    if (snapshot.hostQpc > nowQpc) return ValidationResult::fromFuture;
    if (nowQpc - snapshot.hostQpc > maxAgeQpc) return ValidationResult::stale;
    if (!finiteEye(snapshot.eyes[0], snapshot.validityFlags) ||
        !finiteEye(snapshot.eyes[1], snapshot.validityFlags))
        return ValidationResult::nonFinite;
    return ValidationResult::valid;
}

class Publisher final {
public:
    Publisher() {
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency)) throw std::runtime_error("QueryPerformanceFrequency failed");
        frequency_ = frequency.QuadPart;
        mutex_ = CreateMutexW(nullptr, FALSE, kMutexName);
        if (!mutex_) throw std::runtime_error("CreateMutexW(Shadows pose) failed");
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      static_cast<DWORD>(sizeof(Snapshot)), kMappingName);
        if (!mapping_) {
            CloseHandle(mutex_);
            throw std::runtime_error("CreateFileMappingW(Shadows pose) failed");
        }
        view_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Snapshot));
        if (!view_) {
            CloseHandle(mapping_);
            CloseHandle(mutex_);
            throw std::runtime_error("MapViewOfFile(Shadows pose publisher) failed");
        }
        Snapshot initial{};
        initial.hostQpcFrequency = frequency_;
        if (!writeLocked(initial)) {
            close();
            throw std::runtime_error("initial Shadows pose publication failed");
        }
    }

    ~Publisher() { close(); }
    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;

    bool publish(std::int64_t predictedDisplayTime, std::uint32_t validityFlags,
                 const std::array<EyePose, 2>& eyes) noexcept {
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now)) return false;
        Snapshot snapshot{};
        snapshot.sequence = ++sequence_;
        snapshot.predictedDisplayTime = predictedDisplayTime;
        snapshot.hostQpc = now.QuadPart;
        snapshot.hostQpcFrequency = frequency_;
        snapshot.viewCount = 2;
        snapshot.validityFlags = validityFlags;
        snapshot.eyes[0] = eyes[0];
        snapshot.eyes[1] = eyes[1];
        return writeLocked(snapshot);
    }

private:
    void close() noexcept {
        if (view_) { UnmapViewOfFile(view_); view_ = nullptr; }
        if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
        if (mutex_) { CloseHandle(mutex_); mutex_ = nullptr; }
    }

    bool writeLocked(const Snapshot& snapshot) noexcept {
        const DWORD wait = WaitForSingleObject(mutex_, 50);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return false;
        std::memcpy(view_, &snapshot, sizeof(snapshot));
        return ReleaseMutex(mutex_) != FALSE;
    }

    HANDLE mutex_ = nullptr;
    HANDLE mapping_ = nullptr;
    void* view_ = nullptr;
    std::int64_t frequency_ = 0;
    std::uint64_t sequence_ = 0;
};

class Reader final {
public:
    Reader() = default;
    ~Reader() { close(); }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    bool open() noexcept {
        close();
        mutex_ = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, kMutexName);
        if (!mutex_) return false;
        mapping_ = OpenFileMappingW(FILE_MAP_READ, FALSE, kMappingName);
        if (!mapping_) { close(); return false; }
        view_ = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, sizeof(Snapshot));
        if (!view_) { close(); return false; }
        return true;
    }

    bool read(Snapshot& destination, DWORD timeoutMs = 10) noexcept {
        if (!view_ || !mutex_) return false;
        const DWORD wait = WaitForSingleObject(mutex_, timeoutMs);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return false;
        std::memcpy(&destination, view_, sizeof(destination));
        return ReleaseMutex(mutex_) != FALSE;
    }

    void close() noexcept {
        if (view_) { UnmapViewOfFile(view_); view_ = nullptr; }
        if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
        if (mutex_) { CloseHandle(mutex_); mutex_ = nullptr; }
    }

private:
    HANDLE mutex_ = nullptr;
    HANDLE mapping_ = nullptr;
    const void* view_ = nullptr;
};

} // namespace anvilvr::shadows_pose
