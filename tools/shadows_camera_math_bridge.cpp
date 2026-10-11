#define ANVIL_SHADOWS_CAMERA_BRIDGE_BUILD
#include <anvilvr/ShadowsCameraMathBridge.hpp>
#include <anvilvr/ShadowsPoseTransport.hpp>

#include "../games/Shadows/sdk/EyeCamera.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>

namespace {

using anvilvr::shadows::EyeCameraStatus;
using anvilvr::shadows::FovTangents;
using anvilvr::shadows::GameCameraBasis;
using anvilvr::shadows::MatrixStorage;
using anvilvr::shadows::Pose;
using anvilvr::shadows::Quaternion;
using anvilvr::shadows::Vector3;
using anvilvr::shadows_pose::Snapshot;

constexpr double kQuaternionNormTolerance = 1.0e-3;
constexpr double kHalfPi = 1.57079632679489661923;
constexpr double kAngleMargin = 1.0e-5;

struct BridgeState final {
    std::mutex mutex;
    anvilvr::shadows_pose::Reader reader;
    bool readerOpen{};
    bool recentered{};
    Pose recenter{};
    Snapshot pairSnapshot{};
    bool pairValid{};
};

BridgeState g_state;

[[nodiscard]] bool finite(const double value) noexcept { return std::isfinite(value); }

[[nodiscard]] bool finite_float_array(const float* values, const std::size_t count) noexcept
{
    if (values == nullptr) return false;
    for (std::size_t index = 0; index < count; ++index) {
        if (!std::isfinite(values[index])) return false;
    }
    return true;
}

[[nodiscard]] AnvilShadowsCameraBridgeStatus fail(
    AnvilShadowsCameraBridgeOutput* output,
    const AnvilShadowsCameraBridgeStatus status) noexcept
{
    if (output != nullptr) {
        std::memset(output, 0, sizeof(*output));
        output->status = status;
    }
    return status;
}

[[nodiscard]] bool read_normalized_quaternion(
    const float source[4],
    Quaternion& destination) noexcept
{
    if (!finite_float_array(source, 4)) return false;
    const double normSquared = static_cast<double>(source[0]) * source[0]
        + static_cast<double>(source[1]) * source[1]
        + static_cast<double>(source[2]) * source[2]
        + static_cast<double>(source[3]) * source[3];
    if (!finite(normSquared) || normSquared <= 0.0
        || std::abs(normSquared - 1.0) > kQuaternionNormTolerance) {
        return false;
    }
    const double inverseNorm = 1.0 / std::sqrt(normSquared);
    destination = {
        static_cast<double>(source[0]) * inverseNorm,
        static_cast<double>(source[1]) * inverseNorm,
        static_cast<double>(source[2]) * inverseNorm,
        static_cast<double>(source[3]) * inverseNorm,
    };
    return true;
}

[[nodiscard]] bool read_pose(
    const float orientation[4],
    const float position[3],
    Pose& destination) noexcept
{
    if (!read_normalized_quaternion(orientation, destination.orientation)
        || !finite_float_array(position, 3)) {
        return false;
    }
    destination.position = {position[0], position[1], position[2]};
    return true;
}

[[nodiscard]] bool midpoint_pose(const Snapshot& snapshot, Pose& destination) noexcept
{
    Quaternion left{};
    Quaternion right{};
    if (!read_normalized_quaternion(snapshot.eyes[0].orientation, left)
        || !read_normalized_quaternion(snapshot.eyes[1].orientation, right)) {
        return false;
    }
    const double dot = left.x * right.x + left.y * right.y
        + left.z * right.z + left.w * right.w;
    if (dot < 0.0) {
        right.x = -right.x;
        right.y = -right.y;
        right.z = -right.z;
        right.w = -right.w;
    }
    Quaternion mean{
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
        left.w + right.w,
    };
    const double normSquared = mean.x * mean.x + mean.y * mean.y
        + mean.z * mean.z + mean.w * mean.w;
    if (!finite(normSquared) || normSquared <= 1.0e-12) return false;
    const double inverseNorm = 1.0 / std::sqrt(normSquared);
    mean.x *= inverseNorm;
    mean.y *= inverseNorm;
    mean.z *= inverseNorm;
    mean.w *= inverseNorm;

    destination.orientation = mean;
    destination.position = {
        (static_cast<double>(snapshot.eyes[0].position[0]) + snapshot.eyes[1].position[0]) * 0.5,
        (static_cast<double>(snapshot.eyes[0].position[1]) + snapshot.eyes[1].position[1]) * 0.5,
        (static_cast<double>(snapshot.eyes[0].position[2]) + snapshot.eyes[1].position[2]) * 0.5,
    };
    return finite(destination.position.x) && finite(destination.position.y)
        && finite(destination.position.z);
}

[[nodiscard]] AnvilShadowsCameraBridgeStatus snapshot_status(
    const Snapshot& snapshot,
    const std::int64_t nowQpc,
    const std::int64_t ageDivisor = 10) noexcept
{
    const std::int64_t maxAge = snapshot.hostQpcFrequency > 0
        ? snapshot.hostQpcFrequency / ageDivisor
        : 0;
    const auto validation = anvilvr::shadows_pose::validateSnapshot(snapshot, nowQpc, maxAge);
    if (validation == anvilvr::shadows_pose::ValidationResult::valid) {
        return ANVIL_SHADOWS_CAMERA_BRIDGE_READY;
    }
    if (validation == anvilvr::shadows_pose::ValidationResult::stale
        || validation == anvilvr::shadows_pose::ValidationResult::fromFuture) {
        return ANVIL_SHADOWS_CAMERA_BRIDGE_POSE_STALE;
    }
    return ANVIL_SHADOWS_CAMERA_BRIDGE_POSE_INVALID;
}

[[nodiscard]] AnvilShadowsCameraBridgeStatus read_live_snapshot_locked(
    Snapshot& snapshot) noexcept
{
    if (!g_state.readerOpen) {
        if (!g_state.reader.open()) {
            return ANVIL_SHADOWS_CAMERA_BRIDGE_MAPPING_UNAVAILABLE;
        }
        g_state.readerOpen = true;
    }
    if (!g_state.reader.read(snapshot)) {
        return ANVIL_SHADOWS_CAMERA_BRIDGE_POSE_READ_FAILED;
    }
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) {
        return ANVIL_SHADOWS_CAMERA_BRIDGE_INTERNAL_ERROR;
    }
    return snapshot_status(snapshot, now.QuadPart);
}

[[nodiscard]] AnvilShadowsCameraBridgeStatus parse_fov(
    const float source[4],
    FovTangents& destination) noexcept
{
    if (!finite_float_array(source, 4)) return ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_FOV;
    for (const float angle : std::array<float, 4>{source[0], source[1], source[2], source[3]}) {
        if (!(angle > -kHalfPi + kAngleMargin && angle < kHalfPi - kAngleMargin)) {
            return ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_FOV;
        }
    }
    destination = {
        std::tan(static_cast<double>(source[0])),
        std::tan(static_cast<double>(source[1])),
        std::tan(static_cast<double>(source[3])),
        std::tan(static_cast<double>(source[2])),
    };
    if (!(destination.left < destination.right) || !(destination.down < destination.up)) {
        return ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_FOV;
    }
    return ANVIL_SHADOWS_CAMERA_BRIDGE_READY;
}

[[nodiscard]] AnvilShadowsCameraBridgeStatus map_eye_status(
    const EyeCameraStatus status) noexcept
{
    switch (status) {
    case EyeCameraStatus::ready: return ANVIL_SHADOWS_CAMERA_BRIDGE_READY;
    case EyeCameraStatus::non_finite_input: return ANVIL_SHADOWS_CAMERA_BRIDGE_NON_FINITE_INPUT;
    case EyeCameraStatus::invalid_units_per_metre:
        return ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_UNITS_PER_METRE;
    case EyeCameraStatus::non_orthonormal_basis:
    case EyeCameraStatus::non_positive_basis_determinant:
        return ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_BASE_BASIS;
    case EyeCameraStatus::invalid_quaternion: return ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_POSE;
    case EyeCameraStatus::invalid_clip_planes:
        return ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_CLIP_PLANES;
    case EyeCameraStatus::invalid_fov: return ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_FOV;
    }
    return ANVIL_SHADOWS_CAMERA_BRIDGE_INTERNAL_ERROR;
}

[[nodiscard]] AnvilShadowsCameraBridgeStatus compute(
    const float* anvilC2W290,
    const float* originalPositionLow2D0,
    const double unitsPerMetre,
    const double nearPlane,
    const double farPlane,
    const std::uint32_t eyeIndex,
    const Pose& recenter,
    const Pose& absoluteEye,
    const float fovAngles[4],
    const std::uint64_t sequence,
    const std::int64_t predictedDisplayTime,
    const std::int64_t hostQpc,
    const float sourceOrientation[4],
    const float sourcePosition[3],
    AnvilShadowsCameraBridgeOutput* output) noexcept
{
    if (output == nullptr || anvilC2W290 == nullptr || originalPositionLow2D0 == nullptr) {
        return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_NULL_ARGUMENT);
    }
    if (eyeIndex > 1) return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_EYE);
    if (!finite_float_array(anvilC2W290, 16)
        || !finite_float_array(originalPositionLow2D0, 4)) {
        return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_NON_FINITE_INPUT);
    }
    if (!finite(unitsPerMetre) || unitsPerMetre <= 0.0) {
        return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_UNITS_PER_METRE);
    }
    if (!finite(nearPlane) || !finite(farPlane)
        || nearPlane <= 0.0 || farPlane <= nearPlane) {
        return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_CLIP_PLANES);
    }

    GameCameraBasis base{};
    base.right = {anvilC2W290[0], anvilC2W290[1], anvilC2W290[2]};
    base.backward = {-anvilC2W290[4], -anvilC2W290[5], -anvilC2W290[6]};
    base.up = {anvilC2W290[8], anvilC2W290[9], anvilC2W290[10]};
    base.position = {
        static_cast<double>(anvilC2W290[12]) + originalPositionLow2D0[0],
        static_cast<double>(anvilC2W290[13]) + originalPositionLow2D0[1],
        static_cast<double>(anvilC2W290[14]) + originalPositionLow2D0[2],
    };
    base.units_per_metre = unitsPerMetre;

    FovTangents fov{};
    const auto fovStatus = parse_fov(fovAngles, fov);
    if (fovStatus != ANVIL_SHADOWS_CAMERA_BRIDGE_READY) return fail(output, fovStatus);

    const auto eye = anvilvr::shadows::build_eye_camera_from_absolute_view(
        base, recenter, absoluteEye, fov, nearPlane, farPlane);
    const auto mappedStatus = map_eye_status(eye.status);
    if (mappedStatus != ANVIL_SHADOWS_CAMERA_BRIDGE_READY) return fail(output, mappedStatus);

    AnvilShadowsCameraBridgeOutput result{};
    result.status = ANVIL_SHADOWS_CAMERA_BRIDGE_READY;
    result.eyeIndex = eyeIndex;
    result.poseSequence = sequence;
    result.predictedDisplayTime = predictedDisplayTime;
    result.hostQpc = hostQpc;
    std::copy_n(sourceOrientation, 4, result.sourceAbsoluteEyeOrientation);
    std::copy_n(sourcePosition, 3, result.sourceAbsoluteEyePosition);
    std::copy_n(fovAngles, 4, result.sourceFov);

    // EyeCamera is conventional XR C2W: right, up, backward, position. Convert
    // once to the measured Anvil order: right, forward, up, position.
    const auto& c2w = eye.camera_to_world;
    for (std::size_t row = 0; row < 3; ++row) {
        result.outputAnvilC2W[row] = static_cast<float>(c2w(row, 0));
        result.outputAnvilC2W[4 + row] = static_cast<float>(-c2w(row, 2));
        result.outputAnvilC2W[8 + row] = static_cast<float>(c2w(row, 1));
        const double exactPosition = c2w(row, 3);
        const float positionHigh = static_cast<float>(exactPosition);
        result.outputAnvilC2W[12 + row] = positionHigh;
        result.outputPositionLow[row] = static_cast<float>(exactPosition - positionHigh);

        result.outputRightUpBackFloat4[row] = static_cast<float>(c2w(row, 0));
        result.outputRightUpBackFloat4[4 + row] = static_cast<float>(c2w(row, 1));
        result.outputRightUpBackFloat4[8 + row] = static_cast<float>(c2w(row, 2));
    }
    result.outputAnvilC2W[15] = 1.0f;

    const auto projection = anvilvr::shadows::store_matrix(
        eye.camera_to_clip, MatrixStorage::column_major);
    for (std::size_t index = 0; index < projection.size(); ++index) {
        result.outputXRProjectionColumnMajor[index] = static_cast<float>(projection[index]);
    }
    *output = result;
    return ANVIL_SHADOWS_CAMERA_BRIDGE_READY;
}

} // namespace

std::uint32_t ANVIL_SHADOWS_CAMERA_BRIDGE_CALL AnvilShadowsCameraBridgeComputeSynthetic(
    const float* anvilC2W290,
    const float* originalPositionLow2D0,
    const double unitsPerMetre,
    const double nearPlane,
    const double farPlane,
    const std::uint32_t eyeIndex,
    const AnvilShadowsCameraSyntheticPose* syntheticPose,
    AnvilShadowsCameraBridgeOutput* output) noexcept
{
    try {
        if (syntheticPose == nullptr || output == nullptr || anvilC2W290 == nullptr
            || originalPositionLow2D0 == nullptr) {
            return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_NULL_ARGUMENT);
        }
        Pose recenter{};
        Pose absoluteEye{};
        if (!read_pose(
                syntheticPose->recenterOrientation,
                syntheticPose->recenterPosition,
                recenter)
            || !read_pose(
                syntheticPose->absoluteEyeOrientation,
                syntheticPose->absoluteEyePosition,
                absoluteEye)) {
            return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_POSE);
        }
        return compute(
            anvilC2W290, originalPositionLow2D0,
            unitsPerMetre, nearPlane, farPlane, eyeIndex,
            recenter, absoluteEye, syntheticPose->fov,
            syntheticPose->poseSequence, syntheticPose->predictedDisplayTime,
            syntheticPose->hostQpc, syntheticPose->absoluteEyeOrientation,
            syntheticPose->absoluteEyePosition, output);
    } catch (...) {
        return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INTERNAL_ERROR);
    }
}

std::uint32_t ANVIL_SHADOWS_CAMERA_BRIDGE_CALL AnvilShadowsCameraBridgeComputeLive(
    const float* anvilC2W290,
    const float* originalPositionLow2D0,
    const double unitsPerMetre,
    const double nearPlane,
    const double farPlane,
    const std::uint32_t eyeIndex,
    AnvilShadowsCameraBridgeOutput* output) noexcept
{
    try {
        if (output == nullptr || anvilC2W290 == nullptr || originalPositionLow2D0 == nullptr) {
            return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_NULL_ARGUMENT);
        }
        if (eyeIndex > 1) return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_EYE);

        std::lock_guard<std::mutex> lock(g_state.mutex);
        Snapshot snapshot{};
        const auto status = read_live_snapshot_locked(snapshot);
        if (status != ANVIL_SHADOWS_CAMERA_BRIDGE_READY) return fail(output, status);
        if (!g_state.recentered) {
            if (!midpoint_pose(snapshot, g_state.recenter)) {
                return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_POSE);
            }
            g_state.recentered = true;
        }
        Pose absoluteEye{};
        const auto& sourceEye = snapshot.eyes[eyeIndex];
        if (!read_pose(sourceEye.orientation, sourceEye.position, absoluteEye)) {
            return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_POSE);
        }
        return compute(
            anvilC2W290, originalPositionLow2D0,
            unitsPerMetre, nearPlane, farPlane, eyeIndex,
            g_state.recenter, absoluteEye, sourceEye.fov,
            snapshot.sequence, snapshot.predictedDisplayTime, snapshot.hostQpc,
            sourceEye.orientation, sourceEye.position, output);
    } catch (...) {
        return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INTERNAL_ERROR);
    }
}

std::uint32_t ANVIL_SHADOWS_CAMERA_BRIDGE_CALL AnvilShadowsCameraBridgeComputePairedLive(
    const float* anvilC2W290,
    const float* originalPositionLow2D0,
    const double unitsPerMetre,
    const double nearPlane,
    const double farPlane,
    const std::uint32_t eyeIndex,
    AnvilShadowsCameraBridgeOutput* output) noexcept
{
    try {
        if (output == nullptr || anvilC2W290 == nullptr || originalPositionLow2D0 == nullptr) {
            return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_NULL_ARGUMENT);
        }
        if (eyeIndex > 1) return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_EYE);

        std::lock_guard<std::mutex> lock(g_state.mutex);
        Snapshot snapshot{};
        if (eyeIndex == 0) {
            g_state.pairValid = false;
            const auto status = read_live_snapshot_locked(snapshot);
            if (status != ANVIL_SHADOWS_CAMERA_BRIDGE_READY) return fail(output, status);
            g_state.pairSnapshot = snapshot;
            g_state.pairValid = true;
        } else {
            if (!g_state.pairValid)
                return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_POSE_READ_FAILED);
            snapshot = g_state.pairSnapshot;
            g_state.pairValid = false;
            LARGE_INTEGER now{};
            if (!QueryPerformanceCounter(&now))
                return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INTERNAL_ERROR);
            // The right eye deliberately consumes the left eye latch, not a new pose.
            // Permit one slow/background game-frame interval while retaining
            // its original source timestamp; live eye0 freshness stays100ms.
            const auto status = snapshot_status(snapshot, now.QuadPart, 1);
            if (status != ANVIL_SHADOWS_CAMERA_BRIDGE_READY) return fail(output, status);
        }
        if (!g_state.recentered) {
            if (!midpoint_pose(snapshot, g_state.recenter)) {
                return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_POSE);
            }
            g_state.recentered = true;
        }
        Pose absoluteEye{};
        const auto& sourceEye = snapshot.eyes[eyeIndex];
        if (!read_pose(sourceEye.orientation, sourceEye.position, absoluteEye)) {
            return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_POSE);
        }
        return compute(
            anvilC2W290, originalPositionLow2D0,
            unitsPerMetre, nearPlane, farPlane, eyeIndex,
            g_state.recenter, absoluteEye, sourceEye.fov,
            snapshot.sequence, snapshot.predictedDisplayTime, snapshot.hostQpc,
            sourceEye.orientation, sourceEye.position, output);
    } catch (...) {
        return fail(output, ANVIL_SHADOWS_CAMERA_BRIDGE_INTERNAL_ERROR);
    }
}

std::uint32_t ANVIL_SHADOWS_CAMERA_BRIDGE_CALL AnvilShadowsCameraBridgeReset() noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        g_state.pairValid = false;
        g_state.recentered = false;
        g_state.recenter = {};
        return ANVIL_SHADOWS_CAMERA_BRIDGE_READY;
    } catch (...) {
        return ANVIL_SHADOWS_CAMERA_BRIDGE_INTERNAL_ERROR;
    }
}

std::uint32_t ANVIL_SHADOWS_CAMERA_BRIDGE_CALL AnvilShadowsCameraBridgeRecenter() noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        Snapshot snapshot{};
        const auto status = read_live_snapshot_locked(snapshot);
        if (status != ANVIL_SHADOWS_CAMERA_BRIDGE_READY) return status;
        Pose midpoint{};
        if (!midpoint_pose(snapshot, midpoint)) {
            return ANVIL_SHADOWS_CAMERA_BRIDGE_INVALID_POSE;
        }
        g_state.recenter = midpoint;
        g_state.pairValid = false;
        g_state.recentered = true;
        return ANVIL_SHADOWS_CAMERA_BRIDGE_READY;
    } catch (...) {
        return ANVIL_SHADOWS_CAMERA_BRIDGE_INTERNAL_ERROR;
    }
}

void ANVIL_SHADOWS_CAMERA_BRIDGE_CALL AnvilShadowsCameraBridgeShutdown() noexcept
{
    try {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        g_state.reader.close();
        g_state.readerOpen = false;
        g_state.pairValid = false;
        g_state.recentered = false;
        g_state.recenter = {};
    } catch (...) {
    }
}
