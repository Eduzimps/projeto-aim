#include "aim/core.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near(double actual, double expected, double tolerance = 1e-8) {
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

void projection_tests() {
    const auto identity = aim::Mat4::identity();
    const aim::Viewport viewport{800.0, 600.0};
    const auto center = aim::project({0.0, 0.0, 0.0}, identity, viewport);
    check(center && near(center->x, 400.0) && near(center->y, 300.0),
          "identity projects origin to viewport center");
    const auto upper_right = aim::project({1.0, 1.0, 0.0}, identity, viewport);
    check(upper_right && near(upper_right->x, 800.0) && near(upper_right->y, 0.0),
          "screen y points down while NDC y points up");
    auto translated = identity;
    translated.values[3] = 0.5;
    translated.values[7] = -0.5;
    const auto shifted = aim::project({0.0, 0.0, 0.0}, translated, viewport);
    check(shifted && near(shifted->x, 600.0) && near(shifted->y, 450.0),
          "translation uses last column of row-major matrix");
    auto perspective = identity;
    perspective.values[14] = 1.0;
    perspective.values[15] = 0.0;
    const auto divided = aim::project({1.0, 1.0, 2.0}, perspective, viewport);
    check(divided && near(divided->x, 600.0) && near(divided->y, 150.0),
          "perspective divide uses clip w");
    check(!aim::project({0.0, 0.0, -2.0}, perspective, viewport),
          "negative w rejects points behind camera");
    check(!aim::project({0.0, 0.0, 0.0}, perspective, viewport), "zero w rejected");
    check(!aim::project({0.0, 0.0, 1.01}, identity, viewport), "far depth rejected");
    check(!aim::project({0.0, 0.0, -1.01}, identity, viewport), "near depth rejected");
    check(aim::project({2.0, 0.0, 0.0}, identity, viewport).has_value(),
          "offscreen x stays available to renderer for clipping");
    check(!aim::project({}, identity, {0.0, 600.0}), "zero viewport width rejected");
    auto invalid = identity;
    invalid.values[2] = std::numeric_limits<double>::infinity();
    check(!aim::project({}, invalid, viewport), "nonfinite matrix rejected");
    check(!aim::project({std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0},
                        identity, viewport), "nonfinite world point rejected");
}

void box_and_health_tests() {
    const auto box = aim::make_box({100.0, 300.0}, {100.0, 100.0});
    check(box && near(box->min.x, 55.0) && near(box->max.x, 145.0) &&
          near(box->min.y, 100.0) && near(box->max.y, 300.0),
          "box has configured aspect ratio and vertical bounds");
    check(!aim::make_box({10.0, 0.0}, {10.0, 10.0}), "inverted box rejected");
    check(!aim::make_box({}, {}), "zero-height box rejected");
    check(!aim::make_box({0.0, 20.0}, {}, -0.1), "negative box ratio rejected");
    const auto half = aim::health_fraction(50.0, 100.0);
    const auto dead = aim::health_fraction(0.0, 100.0);
    const auto overheal = aim::health_fraction(150.0, 100.0);
    check(half && near(*half, 0.5), "half health scales to half bar");
    check(dead && near(*dead, 0.0), "zero health is valid");
    check(overheal && near(*overheal, 1.0), "overheal clamps to full bar");
    check(!aim::health_fraction(-1.0, 100.0), "negative current health rejected");
    check(!aim::health_fraction(50.0, 0.0), "zero maximum health rejected");
    check(!aim::health_fraction(50.0, std::numeric_limits<double>::infinity()),
          "nonfinite health rejected");
}

aim::AimConfig enabled_config() {
    aim::AimConfig config;
    config.enabled = true;
    return config;
}

void selection_tests() {
    auto config = enabled_config();
    config.fov_radius_px = 100.0;
    const std::vector<aim::Target> targets{
        {1, {1.0, 0.0}, false, true, true},
        {2, {2.0, 0.0}, true, false, true},
        {3, {3.0, 0.0}, true, true, false},
        {4, {90.0, 90.0}, true, true, true},
        {5, {80.0, 0.0}, true, true, true},
        {6, {75.0, 0.0}, true, true, true}};
    auto selected = aim::select_target(targets, {}, config);
    check(selected && selected->id == 6, "select closest alive visible enemy in circular FOV");
    selected = aim::select_target(targets, {}, config, 5);
    check(selected && selected->id == 5, "retention avoids switching for a small distance advantage");
    config.retention_bias_px = 2.0;
    selected = aim::select_target(targets, {}, config, 5);
    check(selected && selected->id == 6, "a sufficient distance advantage switches targets");
    selected = aim::select_target(targets, {}, config, 4);
    check(selected && selected->id == 6, "retention cannot keep a target outside FOV");
    selected = aim::select_target({{8, {100.0, 0.0}}, {7, {-100.0, 0.0}}}, {}, config);
    check(selected && selected->id == 7, "FOV boundary included and ties use stable id");
    check(!aim::select_target({{1, {80.0, 80.0}}}, {}, config),
          "square-corner point outside circular FOV rejected");
    config.enabled = false;
    check(!aim::select_target(targets, {}, config), "disabled configuration selects no target");
    config.enabled = true;
    config.strength = 1.01;
    check(!config.valid() && !aim::select_target(targets, {}, config),
          "invalid configuration rejected before selection");
    config = enabled_config();
    config.fov_radius_px = std::numeric_limits<double>::quiet_NaN();
    check(!config.valid(), "NaN configuration rejected");
}

void controller_tests() {
    auto config = enabled_config();
    config.strength = 1.0;
    config.max_speed_px_s = 100.0;
    config.max_acceleration_px_s2 = 200.0;
    aim::AimController controller;
    aim::Vec2 previous_velocity{};
    const double dt = 0.01;
    for (int i = 0; i < 100; ++i) {
        const auto movement = controller.step({1000.0, 500.0}, dt, config);
        const auto velocity = controller.velocity();
        check(aim::finite(movement) && aim::finite(velocity), "motion stays finite");
        check(aim::length(movement) <= config.max_speed_px_s * dt + 1e-8,
              "relative movement obeys speed cap");
        check(aim::length(velocity - previous_velocity) <=
                  config.max_acceleration_px_s2 * dt + 1e-8,
              "tracking velocity obeys acceleration cap");
        previous_velocity = velocity;
    }
    controller.reset();
    check(near(aim::length(controller.velocity()), 0.0), "reset clears controller state");

    config = enabled_config();
    config.max_speed_px_s = 1e7;
    config.max_acceleration_px_s2 = 1e9;
    auto simulate = [&](double frame_dt) {
        aim::AimController c;
        aim::Vec2 error{100.0, -50.0};
        const int frames = static_cast<int>(std::lround(1.0 / frame_dt));
        for (int i = 0; i < frames; ++i) {
            const auto movement = c.step(error, frame_dt, config);
            check(aim::length(movement) <= aim::length(error) + 1e-8,
                  "motion does not cross a fixed target");
            error = error - movement;
        }
        return error;
    };
    const auto at_60 = simulate(1.0 / 60.0);
    const auto at_144 = simulate(1.0 / 144.0);
    check(near(at_60.x, at_144.x, 1e-7) && near(at_60.y, at_144.y, 1e-7),
          "unconstrained exponential convergence is independent of frame rate");
    check(near(at_60.x, 100.0 * std::exp(-config.response_rate * config.strength), 1e-7),
          "convergence matches exponential response in seconds");

    config = enabled_config();
    controller.reset();
    aim::Vec2 tracked_error{87.0, -31.0};
    for (int i = 0; i < 1000; ++i) {
        const auto movement = controller.step(tracked_error, 1.0 / 120.0, config);
        const auto remaining = tracked_error - movement;
        check(aim::finite(remaining) &&
              aim::length(remaining) <= aim::length(tracked_error) + 1e-8,
              "acceleration-limited convergence never increases fixed-target error");
        check(remaining.x >= -1e-8 && remaining.y <= 1e-8,
              "acceleration-limited convergence does not pass the target");
        tracked_error = remaining;
    }
    check(aim::length(tracked_error) < 1e-6, "limited controller reaches the target");

    aim::AimController weaker;
    aim::AimController stronger;
    config.max_acceleration_px_s2 = 1e9;
    config.strength = 0.25;
    const auto weak_step = weaker.step({100.0, 0.0}, 1.0 / 60.0, config);
    config.strength = 1.0;
    const auto strong_step = stronger.step({100.0, 0.0}, 1.0 / 60.0, config);
    check(weak_step.x > 0.0 && strong_step.x > weak_step.x,
          "strength setting increases convergence when caps allow it");

    controller.reset();
    const auto almost_zero = controller.step({1e-8, 0.0}, 0.01, config);
    check(aim::finite(almost_zero) && almost_zero.x >= 0.0 && almost_zero.x <= 1e-8,
          "tiny target errors remain finite without overshoot");
    const auto after_jump = controller.step({-1e-8, 0.0}, 0.01, config);
    check(aim::finite(after_jump) && after_jump.x <= 0.0 && after_jump.x >= -1e-8,
          "a sudden opposite error cannot move away from the target");

    const std::vector<double> invalid_dt{0.0, -0.01, 0.251,
        std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()};
    for (double bad_dt : invalid_dt) {
        controller.step({100.0, 0.0}, 0.01, config);
        const auto movement = controller.step({100.0, 0.0}, bad_dt, config);
        check(near(aim::length(movement), 0.0) && near(aim::length(controller.velocity()), 0.0),
              "invalid or stale dt returns zero and clears state");
    }
    config.strength = 0.0;
    check(near(aim::length(controller.step({100.0, 100.0}, 0.01, config)), 0.0),
          "zero strength produces no movement");
    config.strength = 0.5;
    config.enabled = false;
    check(near(aim::length(controller.step({100.0, 100.0}, 0.01, config)), 0.0),
          "disabled aim produces no movement");
    config.enabled = true;
    check(near(aim::length(controller.step({std::numeric_limits<double>::infinity(), 0.0},
                                         0.01, config)), 0.0),
          "nonfinite errors produce no movement");
}

} // namespace

int main() {
    projection_tests();
    box_and_health_tests();
    selection_tests();
    controller_tests();
    if (failures != 0) {
        std::cerr << failures << " core check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All core checks passed\n";
    return EXIT_SUCCESS;
}
