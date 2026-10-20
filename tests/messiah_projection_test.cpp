#include "aim/messiah_projection.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace mp = aim::messiah_projection;
namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool close(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}
mp::Camera identity() {
    return {{{1, 0, 0, 0, 1, 0, 0, 0, 1}}, {}, {1600, 900}, 90};
}
}

int main() {
    auto camera = identity();
    auto point = mp::project({0, 0, -10}, camera);
    check(point && close(point->pixel.x, 800) && close(point->pixel.y, 450) &&
          close(point->depth, 10) && point->in_viewport,
          "forward point projects to viewport center with positive depth");
    point = mp::project({10, 5, -10}, camera);
    check(point && close(point->pixel.x, 1250) && close(point->pixel.y, 225) &&
          point->in_viewport, "vertical FOV scales X by height and flips Y");
    point = mp::project({0, -10, -10}, camera);
    check(point && close(point->pixel.y, 900) && !point->in_viewport,
          "bottom edge is outside the half-open viewport");
    point = mp::project({-30, 0, -10}, camera);
    check(point && close(point->pixel.x, -550) && !point->in_viewport,
          "offscreen forward coordinates are retained for clipping");
    check(!mp::project({0, 0, 10}, camera) && !mp::project({0, 0, 0}, camera),
          "behind-camera and zero-depth points are rejected");

    // Camera looks along positive world X. Its right direction is positive Z.
    // A transposed interpretation of the stored nine floats fails this case.
    camera.rotation = {{0, 0, -1, 0, 1, 0, 1, 0, 0}};
    camera.origin = {100, -20, 30};
    point = mp::project({110, -15, 40}, camera);
    check(point && close(point->pixel.x, 1250) && close(point->pixel.y, 225) &&
          close(point->depth, 10), "rotation order and nonzero origin match derived camera axes");
    check(!mp::project({90, -20, 30}, camera), "rotated camera rejects negative world-X direction");

    camera = identity();
    camera.vertical_fov_degrees = 60;
    point = mp::project({0, 10 / std::sqrt(3.0), -10}, camera);
    check(point && close(point->pixel.y, 0), "vertical FOV sets analytical top-edge angle");
    camera = identity();
    camera.viewport = {900, 900};
    point = mp::project({10, 0, -10}, camera);
    check(point && close(point->pixel.x, 900), "square viewport retains vertical angular scale");
    camera = identity();
    point = mp::project({0, 0, -0.0000001}, camera);
    check(point && close(point->depth, 1e-7) && close(point->pixel.x, 800),
          "positive near-zero depth is retained without near clipping");
    point = mp::project({1e-7, 0, -1e-7}, camera);
    check(point && close(point->pixel.x, 845), "engine denominator floor controls near-plane magnification");
    point = mp::project({0, 0, -1e20}, camera);
    check(point && close(point->depth, 1e20), "positive far depth is retained without far clipping");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const aim::Vec3 world : {aim::Vec3{nan, 0, -1}, aim::Vec3{0, inf, -1},
                                 aim::Vec3{0, 0, -inf}}) {
        check(!mp::project(world, camera), "nonfinite world position is rejected");
    }
    for (const double fov : {0.0, -1.0, 180.0, 181.0, nan, inf}) {
        camera = identity();
        camera.vertical_fov_degrees = fov;
        check(!mp::project({0, 0, -1}, camera), "invalid vertical FOV is rejected");
    }
    for (const aim::Viewport viewport : {aim::Viewport{0, 100}, aim::Viewport{100, -1},
                                        aim::Viewport{nan, 100}, aim::Viewport{100, inf}}) {
        camera = identity();
        camera.viewport = viewport;
        check(!mp::project({0, 0, -1}, camera), "invalid viewport is rejected");
    }
    camera = identity();
    camera.origin.x = nan;
    check(!mp::project({0, 0, -1}, camera), "nonfinite camera origin is rejected");
    camera = identity();
    camera.rotation[6] = std::numeric_limits<float>::infinity();
    check(!mp::project({0, 0, -1}, camera), "nonfinite rotation is rejected");
    camera = identity();
    camera.origin.x = -std::numeric_limits<double>::max();
    check(!mp::project({std::numeric_limits<double>::max(), 0, -1}, camera),
          "overflow while subtracting camera origin is rejected");
    camera = identity();
    camera.viewport.height = std::numeric_limits<double>::max();
    check(!mp::project({100, 0, -1}, camera), "nonfinite projected result is rejected");

    if (failures) { std::cerr << failures << " Messiah projection check(s) failed\n"; return EXIT_FAILURE; }
    std::cout << "All Messiah projection checks passed\n";
    return EXIT_SUCCESS;
}
