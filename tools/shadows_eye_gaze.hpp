#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace anvilvr::shadows_eye_gaze {

struct Vec3 final { float x = 0, y = 0, z = 0; };
struct Quaternion final { float x = 0, y = 0, z = 0, w = 1; };
struct Pose final { Quaternion orientation{}; Vec3 position{}; };
struct Fov final { float left = 0, right = 0, up = 0, down = 0; };
struct Projection final {
    bool valid = false;
    bool inFrustum = false;
    float u = 0.5f;
    float v = 0.5f;
};

inline Quaternion conjugate(Quaternion q) { return {-q.x, -q.y, -q.z, q.w}; }

inline Quaternion normalized(Quaternion q) {
    const float length2 = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (!std::isfinite(length2) || length2 < 1.0e-8f) return {};
    const float inverse = 1.0f / std::sqrt(length2);
    return {q.x*inverse, q.y*inverse, q.z*inverse, q.w*inverse};
}

inline Vec3 rotate(Quaternion raw, Vec3 v) {
    const float rawLength2 = raw.x*raw.x + raw.y*raw.y + raw.z*raw.z + raw.w*raw.w;
    if (!std::isfinite(rawLength2) || rawLength2 < 1.0e-8f)
        return {NAN, NAN, NAN};
    const Quaternion q = normalized(raw);
    const Vec3 t{2.0f*(q.y*v.z - q.z*v.y),
                 2.0f*(q.z*v.x - q.x*v.z),
                 2.0f*(q.x*v.y - q.y*v.x)};
    return {v.x + q.w*t.x + (q.y*t.z - q.z*t.y),
            v.y + q.w*t.y + (q.z*t.x - q.x*t.z),
            v.z + q.w*t.z + (q.x*t.y - q.y*t.x)};
}

// OpenXR gaze and view poses are expressed in the same base space. The -Z
// axis of the gaze pose is the actual gaze ray. Projection is performed in
// the source eye's local frame and with that source eye's asymmetric FOV.
inline Projection project(Quaternion gazeOrientation, const Pose& sourceEye, const Fov& fov) {
    const Vec3 gazeBase = rotate(gazeOrientation, {0, 0, -1});
    const Vec3 gazeEye = rotate(conjugate(normalized(sourceEye.orientation)), gazeBase);
    if (!std::isfinite(gazeEye.x) || !std::isfinite(gazeEye.y) ||
        !std::isfinite(gazeEye.z) || gazeEye.z >= -1.0e-5f)
        return {};

    const float left = std::tan(fov.left);
    const float right = std::tan(fov.right);
    const float up = std::tan(fov.up);
    const float down = std::tan(fov.down);
    if (!(std::isfinite(left) && std::isfinite(right) && std::isfinite(up) &&
          std::isfinite(down) && left < right && down < up))
        return {};

    const float x = -gazeEye.x / gazeEye.z;
    const float y = -gazeEye.y / gazeEye.z;
    Projection result;
    result.valid = true;
    result.u = (x - left) / (right - left);
    result.v = (up - y) / (up - down); // texture V grows downward
    result.inFrustum = result.u >= 0.0f && result.u <= 1.0f &&
                       result.v >= 0.0f && result.v <= 1.0f;
    return result;
}

// Encoded D3D12 R8_UINT rates: 0=1x1, 1=1x2, 5=2x2. CPU plan only;
// applying it requires the real game's RT dimensions and a verified pass.
struct FocusRateMap final {
    std::uint32_t width = 0, height = 0;
    bool focused = false;
    std::vector<std::uint8_t> rates;
};
inline FocusRateMap buildFocusRateMap(std::uint32_t renderWidth, std::uint32_t renderHeight,
    std::uint32_t tileSize, const Fov& fov, const Projection& focus,
    float innerDegrees = 12.0f, float outerDegrees = 24.0f) {
    FocusRateMap result;
    if (!renderWidth || !renderHeight || (tileSize != 8 && tileSize != 16) ||
        renderWidth > 16384 || renderHeight > 16384) return result;
    result.width = (renderWidth + tileSize - 1) / tileSize;
    result.height = (renderHeight + tileSize - 1) / tileSize;
    result.rates.assign(static_cast<std::size_t>(result.width) * result.height, 0);
    const float l = std::tan(fov.left), r = std::tan(fov.right);
    const float u = std::tan(fov.up), d = std::tan(fov.down);
    if (!focus.valid || !focus.inFrustum || !std::isfinite(focus.u) || !std::isfinite(focus.v) ||
        focus.u < 0 || focus.u > 1 || focus.v < 0 || focus.v > 1 ||
        !std::isfinite(l) || !std::isfinite(r) || !std::isfinite(u) || !std::isfinite(d) ||
        !(l < r && d < u) || !std::isfinite(innerDegrees) || !std::isfinite(outerDegrees) ||
        !(innerDegrees > 0 && innerDegrees < outerDegrees && outerDegrees < 90)) return result;
    const auto ray = [=](float x, float y) {
        Vec3 v{l + (r-l)*x, u + (d-u)*y, -1};
        const float inv = 1.0f/std::sqrt(v.x*v.x + v.y*v.y + 1.0f);
        return Vec3{v.x*inv, v.y*inv, -inv};
    };
    const Vec3 center = ray(focus.u, focus.v);
    constexpr float radians = 0.017453292519943295f;
    const float inner = std::cos(innerDegrees*radians), outer = std::cos(outerDegrees*radians);
    for (std::uint32_t y = 0; y < result.height; ++y)
        for (std::uint32_t x = 0; x < result.width; ++x) {
            const Vec3 direction = ray(
                std::min(static_cast<float>(renderWidth)-0.5f, (static_cast<float>(x)+0.5f)*static_cast<float>(tileSize))/static_cast<float>(renderWidth),
                std::min(static_cast<float>(renderHeight)-0.5f, (static_cast<float>(y)+0.5f)*static_cast<float>(tileSize))/static_cast<float>(renderHeight));
            const float dot = direction.x*center.x + direction.y*center.y + direction.z*center.z;
            result.rates[static_cast<std::size_t>(y)*result.width+x] = dot >= inner ? 0 : dot >= outer ? 1 : 5;
        }
    result.focused = true;
    return result;
}

} // namespace anvilvr::shadows_eye_gaze
