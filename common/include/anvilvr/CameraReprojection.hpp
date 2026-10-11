#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace anvilvr {

// Matrix storage is row-major. All math in this interface uses column vectors:
// clip = world_to_clip * world, and C(row, column).
struct Matrix4 final {
    std::array<double, 16> values{};

    [[nodiscard]] static constexpr Matrix4 identity() noexcept
    {
        Matrix4 result{};
        result(0, 0) = 1.0;
        result(1, 1) = 1.0;
        result(2, 2) = 1.0;
        result(3, 3) = 1.0;
        return result;
    }

    [[nodiscard]] constexpr double& operator()(std::size_t row, std::size_t column) noexcept
    {
        return values[row * 4 + column];
    }

    [[nodiscard]] constexpr double operator()(std::size_t row, std::size_t column) const noexcept
    {
        return values[row * 4 + column];
    }
};

struct CameraFrame final {
    Matrix4 world_to_clip{Matrix4::identity()};
    std::uint64_t frame_id{};
    std::int64_t timestamp_ns{};
};

struct AerSourceFrame final {
    CameraFrame camera{};
    bool gpu_complete{};
    bool depth_valid{};
};

enum class ReprojectionStatus : std::uint8_t {
    ready,
    gpu_incomplete,
    depth_invalid,
    invalid_timestamp,
    source_from_future,
    source_too_old,
    non_finite_matrix,
    non_invertible_source,
};

struct EyeReprojection final {
    // Maps homogeneous source clip coordinates into target clip coordinates.
    // source_clip_to_target_clip = target.world_to_clip * inverse(source.world_to_clip)
    Matrix4 source_clip_to_target_clip{Matrix4::identity()};
    std::uint64_t source_frame_id{};
    std::uint64_t target_frame_id{};
    ReprojectionStatus status{ReprojectionStatus::gpu_incomplete};

    [[nodiscard]] constexpr bool usable() const noexcept
    {
        return status == ReprojectionStatus::ready;
    }
};

using StereoReprojection = std::array<EyeReprojection, 2>;

[[nodiscard]] Matrix4 multiply(const Matrix4& left, const Matrix4& right) noexcept;

// A failure returns identity and an explicit status. max_source_age_ns < 0 disables
// the age limit, but a source timestamp later than its target is always rejected.
[[nodiscard]] EyeReprojection build_camera_reprojection(
    const AerSourceFrame& source,
    const CameraFrame& target,
    std::int64_t max_source_age_ns) noexcept;

// Each eye is evaluated independently. In alternating-eye rendering the two
// source frames are expected to carry different frame_id/timestamp values.
[[nodiscard]] StereoReprojection build_stereo_reprojection(
    const std::array<AerSourceFrame, 2>& sources,
    const std::array<CameraFrame, 2>& targets,
    std::int64_t max_source_age_ns) noexcept;

} // namespace anvilvr
