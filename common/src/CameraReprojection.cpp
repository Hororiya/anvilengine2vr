#include <anvilvr/CameraReprojection.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace anvilvr {
namespace {

[[nodiscard]] bool finite(const Matrix4& matrix) noexcept
{
    return std::ranges::all_of(matrix.values, [](const double value) { return std::isfinite(value); });
}

[[nodiscard]] bool invert(const Matrix4& input, Matrix4& output) noexcept
{
    // Gauss-Jordan elimination with partial pivoting. Keeping the reference
    // implementation independent of a graphics math package makes its layout
    // and multiplication convention testable on every build host.
    double augmented[4][8]{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            augmented[row][column] = input(row, column);
        }
        augmented[row][row + 4] = 1.0;
    }

    constexpr double epsilon = 64.0 * std::numeric_limits<double>::epsilon();
    for (std::size_t column = 0; column < 4; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < 4; ++row) {
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(augmented[pivot][column]) <= epsilon) {
            return false;
        }
        if (pivot != column) {
            for (std::size_t element = 0; element < 8; ++element) {
                std::swap(augmented[pivot][element], augmented[column][element]);
            }
        }

        const double divisor = augmented[column][column];
        for (double& element : augmented[column]) {
            element /= divisor;
        }
        for (std::size_t row = 0; row < 4; ++row) {
            if (row == column) {
                continue;
            }
            const double scale = augmented[row][column];
            for (std::size_t element = 0; element < 8; ++element) {
                augmented[row][element] -= scale * augmented[column][element];
            }
        }
    }

    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            output(row, column) = augmented[row][column + 4];
        }
    }
    return finite(output);
}

} // namespace

Matrix4 multiply(const Matrix4& left, const Matrix4& right) noexcept
{
    Matrix4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t inner = 0; inner < 4; ++inner) {
                result(row, column) += left(row, inner) * right(inner, column);
            }
        }
    }
    return result;
}

EyeReprojection build_camera_reprojection(
    const AerSourceFrame& source,
    const CameraFrame& target,
    const std::int64_t max_source_age_ns) noexcept
{
    EyeReprojection result{};
    result.source_frame_id = source.camera.frame_id;
    result.target_frame_id = target.frame_id;

    if (!source.gpu_complete) {
        result.status = ReprojectionStatus::gpu_incomplete;
        return result;
    }
    if (!source.depth_valid) {
        result.status = ReprojectionStatus::depth_invalid;
        return result;
    }
    if (source.camera.timestamp_ns < 0 || target.timestamp_ns < 0) {
        result.status = ReprojectionStatus::invalid_timestamp;
        return result;
    }
    if (source.camera.timestamp_ns > target.timestamp_ns || source.camera.frame_id > target.frame_id) {
        result.status = ReprojectionStatus::source_from_future;
        return result;
    }
    const auto age = static_cast<std::uint64_t>(target.timestamp_ns) -
        static_cast<std::uint64_t>(source.camera.timestamp_ns);
    if (max_source_age_ns >= 0 && age > static_cast<std::uint64_t>(max_source_age_ns)) {
        result.status = ReprojectionStatus::source_too_old;
        return result;
    }
    if (!finite(source.camera.world_to_clip) || !finite(target.world_to_clip)) {
        result.status = ReprojectionStatus::non_finite_matrix;
        return result;
    }

    Matrix4 clip_to_world{};
    if (!invert(source.camera.world_to_clip, clip_to_world)) {
        result.status = ReprojectionStatus::non_invertible_source;
        return result;
    }

    result.source_clip_to_target_clip = multiply(target.world_to_clip, clip_to_world);
    if (!finite(result.source_clip_to_target_clip)) {
        result.source_clip_to_target_clip = Matrix4::identity();
        result.status = ReprojectionStatus::non_finite_matrix;
        return result;
    }
    result.status = ReprojectionStatus::ready;
    return result;
}

StereoReprojection build_stereo_reprojection(
    const std::array<AerSourceFrame, 2>& sources,
    const std::array<CameraFrame, 2>& targets,
    const std::int64_t max_source_age_ns) noexcept
{
    return {
        build_camera_reprojection(sources[0], targets[0], max_source_age_ns),
        build_camera_reprojection(sources[1], targets[1], max_source_age_ns),
    };
}

} // namespace anvilvr
