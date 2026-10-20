#pragma once

#include "aim/core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace aim::messiah_projection {

// Adapter for the statically inspected ICamera.GetScreenPointFromWorldPoint
// implementation at RVA 0x275db0 of the client identified in camera_profile.hpp.
// These are the first nine floats of ViewMatrix, in their original order.
// Origin is read separately; ViewMatrix's final three floats are not used.
struct Camera {
    std::array<float, 9> rotation{};
    Vec3 origin;
    Viewport viewport;
    double vertical_fov_degrees = 0.0;
};

struct ScreenPoint {
    Vec2 pixel;
    double depth = 0.0;
    bool in_viewport = false;
};

// Top-left pixel origin, positive screen Y down, negative camera Z forward.
// Computes in double precision without the engine's tenth-pixel quantization;
// it is a mathematical adapter, not a bit-for-bit emulation of the engine.
// Near/far clipping is deliberately absent, matching the inspected method.
// Offscreen points in front are returned with in_viewport=false. Points on or
// behind the camera plane and invalid/nonfinite inputs or results are rejected.
inline std::optional<ScreenPoint> project(Vec3 world, const Camera& camera) noexcept {
    if (!finite(world) || !finite(camera.origin) ||
        !std::isfinite(camera.viewport.width) || !std::isfinite(camera.viewport.height) ||
        camera.viewport.width <= 0.0 || camera.viewport.height <= 0.0 ||
        !std::isfinite(camera.vertical_fov_degrees) ||
        camera.vertical_fov_degrees <= 0.0 || camera.vertical_fov_degrees >= 180.0) {
        return std::nullopt;
    }
    for (float value : camera.rotation) {
        if (!std::isfinite(value)) return std::nullopt;
    }
    const Vec3 delta{world.x - camera.origin.x, world.y - camera.origin.y,
                     world.z - camera.origin.z};
    if (!finite(delta)) return std::nullopt;
    const auto& m = camera.rotation;
    const double x = delta.x * m[0] + delta.y * m[3] + delta.z * m[6];
    const double y = delta.x * m[1] + delta.y * m[4] + delta.z * m[7];
    const double z = delta.x * m[2] + delta.y * m[5] + delta.z * m[8];
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || z >= 0.0) {
        return std::nullopt;
    }
    constexpr double degrees_to_radians = 0.017453292519943295769236907684886;
    const double cotangent = 1.0 / std::tan(camera.vertical_fov_degrees *
                                          degrees_to_radians * 0.5);
    const double denominator = std::max(-z, 1e-6);
    const Vec2 pixel{
        0.5 * (camera.viewport.width + camera.viewport.height * (x / denominator) * cotangent),
        0.5 * (camera.viewport.height - camera.viewport.height * (y / denominator) * cotangent)
    };
    if (!finite(pixel)) return std::nullopt;
    const bool in_viewport = pixel.x >= 0.0 && pixel.x < camera.viewport.width &&
                             pixel.y >= 0.0 && pixel.y < camera.viewport.height;
    return ScreenPoint{pixel, -z, in_viewport};
}

} // namespace aim::messiah_projection
