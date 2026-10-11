#include "EyeCamera.hpp"

#include <algorithm>
#include <cmath>

namespace anvilvr::shadows {
namespace {

constexpr double basis_tolerance = 1.0e-5;
constexpr double quaternion_tolerance = 1.0e-5;

[[nodiscard]] bool finite(const double value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const Vector3& value) noexcept
{
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] bool finite(const Quaternion& value) noexcept
{
    return finite(value.x) && finite(value.y) && finite(value.z) && finite(value.w);
}

[[nodiscard]] bool finite(const Pose& value) noexcept
{
    return finite(value.orientation) && finite(value.position);
}

[[nodiscard]] double dot(const Vector3& left, const Vector3& right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] Vector3 cross(const Vector3& left, const Vector3& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] Vector3 add(const Vector3& left, const Vector3& right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] Vector3 negate(const Vector3& value) noexcept
{
    return {-value.x, -value.y, -value.z};
}

[[nodiscard]] Vector3 scale(const Vector3& value, const double factor) noexcept
{
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] double quaternion_norm_squared(const Quaternion& value) noexcept
{
    return value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
}

[[nodiscard]] bool valid_quaternion(const Quaternion& value) noexcept
{
    return finite(value) && std::abs(quaternion_norm_squared(value) - 1.0) <= quaternion_tolerance;
}

[[nodiscard]] Quaternion conjugate(const Quaternion& value) noexcept
{
    return {-value.x, -value.y, -value.z, value.w};
}

[[nodiscard]] Quaternion multiply_quaternion(
    const Quaternion& left,
    const Quaternion& right) noexcept
{
    return {
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
    };
}

[[nodiscard]] Vector3 rotate(const Quaternion& rotation, const Vector3& value) noexcept
{
    const Vector3 qv{rotation.x, rotation.y, rotation.z};
    const auto twice_cross = scale(cross(qv, value), 2.0);
    return add(value, add(scale(twice_cross, rotation.w), cross(qv, twice_cross)));
}

[[nodiscard]] Pose compose(const Pose& parent_from_local, const Pose& local_from_child) noexcept
{
    return {
        multiply_quaternion(parent_from_local.orientation, local_from_child.orientation),
        add(parent_from_local.position, rotate(parent_from_local.orientation, local_from_child.position)),
    };
}

[[nodiscard]] Pose inverse(const Pose& parent_from_local) noexcept
{
    const auto inverse_orientation = conjugate(parent_from_local.orientation);
    return {inverse_orientation, rotate(inverse_orientation, negate(parent_from_local.position))};
}

[[nodiscard]] Vector3 basis_transform(
    const GameCameraBasis& base,
    const Vector3& local) noexcept
{
    return add(add(scale(base.right, local.x), scale(base.up, local.y)), scale(base.backward, local.z));
}

[[nodiscard]] bool orthonormal(const GameCameraBasis& base) noexcept
{
    const auto unit = [](const Vector3& value) noexcept {
        return std::abs(dot(value, value) - 1.0) <= basis_tolerance;
    };
    return unit(base.right) && unit(base.up) && unit(base.backward)
        && std::abs(dot(base.right, base.up)) <= basis_tolerance
        && std::abs(dot(base.right, base.backward)) <= basis_tolerance
        && std::abs(dot(base.up, base.backward)) <= basis_tolerance;
}

[[nodiscard]] double basis_determinant(const GameCameraBasis& base) noexcept
{
    return dot(base.right, cross(base.up, base.backward));
}

[[nodiscard]] Matrix4 quaternion_matrix(const Quaternion& q) noexcept
{
    Matrix4 result = Matrix4::identity();
    const double xx = q.x * q.x;
    const double yy = q.y * q.y;
    const double zz = q.z * q.z;
    const double xy = q.x * q.y;
    const double xz = q.x * q.z;
    const double yz = q.y * q.z;
    const double wx = q.w * q.x;
    const double wy = q.w * q.y;
    const double wz = q.w * q.z;
    result(0, 0) = 1.0 - 2.0 * (yy + zz);
    result(0, 1) = 2.0 * (xy - wz);
    result(0, 2) = 2.0 * (xz + wy);
    result(1, 0) = 2.0 * (xy + wz);
    result(1, 1) = 1.0 - 2.0 * (xx + zz);
    result(1, 2) = 2.0 * (yz - wx);
    result(2, 0) = 2.0 * (xz - wy);
    result(2, 1) = 2.0 * (yz + wx);
    result(2, 2) = 1.0 - 2.0 * (xx + yy);
    return result;
}

[[nodiscard]] Matrix4 make_projection(
    const FovTangents& fov,
    const double near_plane,
    const double far_plane) noexcept
{
    const double horizontal = fov.right - fov.left;
    const double vertical = fov.up - fov.down;
    Matrix4 result{};
    result(0, 0) = 2.0 / horizontal;
    result(0, 2) = (fov.right + fov.left) / horizontal;
    result(1, 1) = 2.0 / vertical;
    result(1, 2) = (fov.up + fov.down) / vertical;
    result(2, 2) = far_plane / (near_plane - far_plane);
    result(2, 3) = near_plane * far_plane / (near_plane - far_plane);
    result(3, 2) = -1.0;
    return result;
}

[[nodiscard]] EyeCamera build(
    const GameCameraBasis& base,
    const Pose& recenter_pose,
    const Pose& absolute_eye_pose,
    const FovTangents& fov,
    const double near_plane,
    const double far_plane) noexcept
{
    EyeCamera result{};
    if (!finite(base.right) || !finite(base.up) || !finite(base.backward)
        || !finite(base.position) || !finite(base.units_per_metre)
        || !finite(recenter_pose) || !finite(absolute_eye_pose)
        || !finite(fov.left) || !finite(fov.right) || !finite(fov.down) || !finite(fov.up)
        || !finite(near_plane) || !finite(far_plane)) {
        result.status = EyeCameraStatus::non_finite_input;
        return result;
    }
    if (base.units_per_metre <= 0.0) {
        result.status = EyeCameraStatus::invalid_units_per_metre;
        return result;
    }
    if (!orthonormal(base)) {
        result.status = EyeCameraStatus::non_orthonormal_basis;
        return result;
    }
    if (basis_determinant(base) <= 0.0) {
        result.status = EyeCameraStatus::non_positive_basis_determinant;
        return result;
    }
    if (!valid_quaternion(recenter_pose.orientation)
        || !valid_quaternion(absolute_eye_pose.orientation)) {
        result.status = EyeCameraStatus::invalid_quaternion;
        return result;
    }
    if (near_plane <= 0.0 || far_plane <= near_plane) {
        result.status = EyeCameraStatus::invalid_clip_planes;
        return result;
    }
    if (!(fov.left < fov.right) || !(fov.down < fov.up)) {
        result.status = EyeCameraStatus::invalid_fov;
        return result;
    }

    const Pose recentered_eye = compose(inverse(recenter_pose), absolute_eye_pose);
    const Matrix4 local_rotation = quaternion_matrix(recentered_eye.orientation);

    Matrix4 camera_to_world = Matrix4::identity();
    for (std::size_t column = 0; column < 3; ++column) {
        const Vector3 local_column{
            local_rotation(0, column),
            local_rotation(1, column),
            local_rotation(2, column),
        };
        const Vector3 world_column = basis_transform(base, local_column);
        camera_to_world(0, column) = world_column.x;
        camera_to_world(1, column) = world_column.y;
        camera_to_world(2, column) = world_column.z;
    }

    const Vector3 translated = basis_transform(base, scale(recentered_eye.position, base.units_per_metre));
    result.world_position = add(base.position, translated);
    camera_to_world(0, 3) = result.world_position.x;
    camera_to_world(1, 3) = result.world_position.y;
    camera_to_world(2, 3) = result.world_position.z;
    result.camera_to_world = camera_to_world;

    Matrix4 world_to_camera = Matrix4::identity();
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            world_to_camera(row, column) = camera_to_world(column, row);
        }
    }
    for (std::size_t row = 0; row < 3; ++row) {
        world_to_camera(row, 3) = -(
            world_to_camera(row, 0) * result.world_position.x
            + world_to_camera(row, 1) * result.world_position.y
            + world_to_camera(row, 2) * result.world_position.z);
    }
    result.world_to_camera = world_to_camera;
    result.camera_to_clip = make_projection(fov, near_plane, far_plane);
    result.world_to_clip = anvilvr::multiply(result.camera_to_clip, result.world_to_camera);
    result.status = EyeCameraStatus::ready;
    return result;
}

} // namespace

EyeCamera build_eye_camera_from_absolute_view(
    const GameCameraBasis& base,
    const Pose& recenter_pose,
    const Pose& absolute_eye_pose,
    const FovTangents& fov,
    const double near_plane,
    const double far_plane) noexcept
{
    return build(base, recenter_pose, absolute_eye_pose, fov, near_plane, far_plane);
}

EyeCamera build_eye_camera_from_head_and_eye(
    const GameCameraBasis& base,
    const Pose& recenter_pose,
    const Pose& absolute_head_pose,
    const Pose& eye_from_head,
    const FovTangents& fov,
    const double near_plane,
    const double far_plane) noexcept
{
    if (!finite(absolute_head_pose) || !finite(eye_from_head)) {
        EyeCamera result{};
        result.status = EyeCameraStatus::non_finite_input;
        return result;
    }
    if (!valid_quaternion(absolute_head_pose.orientation)
        || !valid_quaternion(eye_from_head.orientation)) {
        EyeCamera result{};
        result.status = EyeCameraStatus::invalid_quaternion;
        return result;
    }
    return build(
        base,
        recenter_pose,
        compose(absolute_head_pose, eye_from_head),
        fov,
        near_plane,
        far_plane);
}

std::array<double, 16> store_matrix(
    const Matrix4& matrix,
    const MatrixStorage storage) noexcept
{
    std::array<double, 16> result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            const std::size_t index = storage == MatrixStorage::row_major
                ? row * 4 + column
                : column * 4 + row;
            result[index] = matrix(row, column);
        }
    }
    return result;
}

Matrix4 load_matrix(
    const std::array<double, 16>& stored,
    const MatrixStorage storage) noexcept
{
    Matrix4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            const std::size_t index = storage == MatrixStorage::row_major
                ? row * 4 + column
                : column * 4 + row;
            result(row, column) = stored[index];
        }
    }
    return result;
}

} // namespace anvilvr::shadows
