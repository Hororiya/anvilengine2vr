#pragma once

#include <anvilvr/CameraReprojection.hpp>

#include <array>
#include <cstdint>

namespace anvilvr::shadows {

struct Vector3 final {
    double x{};
    double y{};
    double z{};
};

// Hamilton quaternion (x, y, z, w). It rotates a column vector from pose-local
// space into the pose's parent space.
struct Quaternion final {
    double x{};
    double y{};
    double z{};
    double w{1.0};
};

struct Pose final {
    Quaternion orientation{};
    Vector3 position{};
};

// Measured game camera-to-world convention. The three directions are columns:
// XR camera-local +X/right, +Y/up, +Z/backward (-forward) expressed in game
// world space. Position is already in game-world units.
struct GameCameraBasis final {
    Vector3 right{1.0, 0.0, 0.0};
    Vector3 up{0.0, 1.0, 0.0};
    Vector3 backward{0.0, 0.0, 1.0};
    Vector3 position{};
    double units_per_metre{1.0};
};

// Tangents of OpenXR-style FOV angles. left/down are normally negative and
// right/up positive. The projection looks down camera-local -Z.
struct FovTangents final {
    double left{-1.0};
    double right{1.0};
    double down{-1.0};
    double up{1.0};
};

enum class EyeCameraStatus : std::uint8_t {
    ready,
    non_finite_input,
    invalid_units_per_metre,
    non_orthonormal_basis,
    non_positive_basis_determinant,
    invalid_quaternion,
    invalid_clip_planes,
    invalid_fov,
};

struct EyeCamera final {
    Matrix4 camera_to_world{Matrix4::identity()};
    Matrix4 world_to_camera{Matrix4::identity()};
    Matrix4 camera_to_clip{Matrix4::identity()};
    Matrix4 world_to_clip{Matrix4::identity()};
    Vector3 world_position{};
    EyeCameraStatus status{EyeCameraStatus::non_finite_input};

    [[nodiscard]] constexpr bool usable() const noexcept
    {
        return status == EyeCameraStatus::ready;
    }
};

// Use this entry point when xrLocateViews supplies an absolute eye pose in the
// same local/reference space as recenter_pose. It deliberately has no head-pose
// parameter, preventing head rotation/translation from being applied twice.
[[nodiscard]] EyeCamera build_eye_camera_from_absolute_view(
    const GameCameraBasis& base,
    const Pose& recenter_pose,
    const Pose& absolute_eye_pose,
    const FovTangents& fov,
    double near_plane,
    double far_plane) noexcept;

// Use this entry point only when the caller has a head pose plus a genuinely
// head-relative eye transform. It composes inverse(recenter) * head * eye once.
[[nodiscard]] EyeCamera build_eye_camera_from_head_and_eye(
    const GameCameraBasis& base,
    const Pose& recenter_pose,
    const Pose& absolute_head_pose,
    const Pose& eye_from_head,
    const FovTangents& fov,
    double near_plane,
    double far_plane) noexcept;

enum class MatrixStorage : std::uint8_t {
    row_major,
    column_major,
};

// These adapters change storage only; they never transpose the mathematical
// transform. Matrix4 math remains row/column indexed with column vectors.
[[nodiscard]] std::array<double, 16> store_matrix(
    const Matrix4& matrix,
    MatrixStorage storage) noexcept;

[[nodiscard]] Matrix4 load_matrix(
    const std::array<double, 16>& stored,
    MatrixStorage storage) noexcept;

} // namespace anvilvr::shadows
