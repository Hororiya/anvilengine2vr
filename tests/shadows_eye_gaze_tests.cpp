#include "../tools/shadows_eye_gaze.hpp"

#include <cassert>
#include <cmath>

using namespace anvilvr::shadows_eye_gaze;

namespace {
Quaternion yaw(float radians) { return {0, std::sin(radians*0.5f), 0, std::cos(radians*0.5f)}; }
Quaternion pitch(float radians) { return {std::sin(radians*0.5f), 0, 0, std::cos(radians*0.5f)}; }
bool near(float a, float b) { return std::abs(a-b) < 1.0e-4f; }
}

int main() {
    const Pose eye{};
    const Fov fov{-0.8f, 0.8f, 0.7f, -0.7f};
    const auto center = project({}, eye, fov);
    assert(center.valid && center.inFrustum && near(center.u, 0.5f) && near(center.v, 0.5f));

    const auto left = project(yaw(0.25f), eye, fov);
    const auto right = project(yaw(-0.25f), eye, fov);
    assert(left.valid && right.valid && left.u < center.u && right.u > center.u);

    const auto up = project(pitch(0.2f), eye, fov);
    assert(up.valid && up.v < center.v);

    const auto outside = project(yaw(1.2f), eye, fov);
    assert(outside.valid && !outside.inFrustum);

    const Fov asymmetric{-1.0f, 0.6f, 0.8f, -0.5f};
    const auto asymmetricCenter = project({}, eye, asymmetric);
    assert(asymmetricCenter.valid && asymmetricCenter.inFrustum && !near(asymmetricCenter.u, 0.5f));

    const Pose turnedEye{yaw(0.2f), {}};
    const auto sourcePoseRelative = project(yaw(0.2f), turnedEye, fov);
    assert(sourcePoseRelative.valid && near(sourcePoseRelative.u, 0.5f));

    const auto behind = project(yaw(3.14159265f), eye, fov);
    assert(!behind.valid && !behind.inFrustum);

    const auto invalid = project({NAN, 0, 0, 1}, eye, fov);
    assert(!invalid.valid);
    assert(!project({}, eye, {0.2f, -0.2f, 0.7f, -0.7f}).valid);
    return 0;
}
