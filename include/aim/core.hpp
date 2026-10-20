#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace aim {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double scale) noexcept { return {a.x * scale, a.y * scale}; }
inline double length(Vec2 v) noexcept { return std::hypot(v.x, v.y); }
inline bool finite(Vec2 v) noexcept { return std::isfinite(v.x) && std::isfinite(v.y); }
inline bool finite(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Row-major storage, column vectors: clip = view_projection * [x, y, z, 1].
// Depth follows the OpenGL convention (-w <= z <= w). Adapters must convert
// matrices with other conventions before calling project().
struct Mat4 {
    std::array<double, 16> values{};

    static Mat4 identity() noexcept {
        return {{{1.0, 0.0, 0.0, 0.0,
                  0.0, 1.0, 0.0, 0.0,
                  0.0, 0.0, 1.0, 0.0,
                  0.0, 0.0, 0.0, 1.0}}};
    }
};

struct Viewport {
    double width = 0.0;
    double height = 0.0;
};

// Pixel origin is the upper-left corner. Offscreen x/y coordinates are allowed
// so a renderer can clip a partially visible box. Invalid depth/w is rejected.
inline std::optional<Vec2> project(Vec3 world, const Mat4& matrix,
                                  Viewport viewport) noexcept {
    if (!finite(world) || !std::isfinite(viewport.width) ||
        !std::isfinite(viewport.height) || viewport.width <= 0.0 ||
        viewport.height <= 0.0) {
        return std::nullopt;
    }
    for (double value : matrix.values) {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
    }
    const auto& m = matrix.values;
    const double x = m[0] * world.x + m[1] * world.y + m[2] * world.z + m[3];
    const double y = m[4] * world.x + m[5] * world.y + m[6] * world.z + m[7];
    const double z = m[8] * world.x + m[9] * world.y + m[10] * world.z + m[11];
    const double w = m[12] * world.x + m[13] * world.y + m[14] * world.z + m[15];
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(w) || w <= 1e-8 || z < -w || z > w) {
        return std::nullopt;
    }
    const Vec2 screen{(x / w * 0.5 + 0.5) * viewport.width,
                      (0.5 - y / w * 0.5) * viewport.height};
    return finite(screen) ? std::optional<Vec2>{screen} : std::nullopt;
}

struct ScreenBox {
    Vec2 min;
    Vec2 max;
};

inline std::optional<ScreenBox> make_box(Vec2 feet, Vec2 head,
                                        double width_ratio = 0.45) noexcept {
    if (!finite(feet) || !finite(head) || !std::isfinite(width_ratio) ||
        width_ratio <= 0.0 || width_ratio > 2.0 || feet.y <= head.y) {
        return std::nullopt;
    }
    const double height = feet.y - head.y;
    const double half_width = height * width_ratio * 0.5;
    const double center_x = feet.x * 0.5 + head.x * 0.5;
    const ScreenBox box{{center_x - half_width, head.y},
                        {center_x + half_width, feet.y}};
    if (!std::isfinite(height) || !finite(box.min) || !finite(box.max)) {
        return std::nullopt;
    }
    return box;
}

// Negative health or a nonpositive maximum is invalid; overheal fills the bar.
inline std::optional<double> health_fraction(double current, double maximum) noexcept {
    if (!std::isfinite(current) || !std::isfinite(maximum) || current < 0.0 ||
        maximum <= 0.0) {
        return std::nullopt;
    }
    return current >= maximum ? 1.0 : current / maximum;
}

struct AimConfig {
    bool enabled = false;
    double fov_radius_px = 140.0;
    double strength = 0.6;
    double response_rate = 12.0; // Exponential convergence rate, in 1/seconds.
    double max_speed_px_s = 1600.0;
    double max_acceleration_px_s2 = 12000.0;
    double retention_bias_px = 12.0;

    bool valid() const noexcept {
        return std::isfinite(fov_radius_px) && fov_radius_px > 0.0 &&
               fov_radius_px <= 1e6 && std::isfinite(strength) &&
               strength >= 0.0 && strength <= 1.0 &&
               std::isfinite(response_rate) && response_rate > 0.0 &&
               response_rate <= 1e3 && std::isfinite(max_speed_px_s) &&
               max_speed_px_s > 0.0 && max_speed_px_s <= 1e7 &&
               std::isfinite(max_acceleration_px_s2) &&
               max_acceleration_px_s2 > 0.0 && max_acceleration_px_s2 <= 1e9 &&
               std::isfinite(retention_bias_px) && retention_bias_px >= 0.0 &&
               retention_bias_px <= 1e6;
    }
};

struct Target {
    std::uint64_t id = 0;
    Vec2 screen;
    bool alive = true;
    bool enemy = true;
    bool visible = true;
};

// Selection is circular in pixel space. Retention never extends the FOV or
// bypasses filters; it only tolerates a slightly closer competing target.
inline std::optional<Target> select_target(
    const std::vector<Target>& targets, Vec2 crosshair, const AimConfig& config,
    std::optional<std::uint64_t> retained_id = std::nullopt) noexcept {
    if (!config.enabled || !config.valid() || !finite(crosshair)) {
        return std::nullopt;
    }
    std::optional<Target> best;
    std::optional<Target> retained;
    double best_distance = config.fov_radius_px;
    double retained_distance = 0.0;
    for (const Target& target : targets) {
        if (!target.alive || !target.enemy || !target.visible || !finite(target.screen)) {
            continue;
        }
        const double distance = length(target.screen - crosshair);
        if (!std::isfinite(distance) || distance > config.fov_radius_px) {
            continue;
        }
        if (!best || distance < best_distance ||
            (distance == best_distance && target.id < best->id)) {
            best = target;
            best_distance = distance;
        }
        if (retained_id && target.id == *retained_id) {
            retained = target;
            retained_distance = distance;
        }
    }
    if (best && retained && retained_distance <= best_distance + config.retention_bias_px) {
        return retained;
    }
    return best;
}

class AimController {
public:
    static constexpr double max_delta_time = 0.25;

    void reset() noexcept { velocity_ = {}; }
    Vec2 velocity() const noexcept { return velocity_; }

    // Pure math: returns fractional RELATIVE pixel movement, never injects input.
    // The exponential response uses seconds, not frames. Acceleration limits
    // changes of velocity during tracking. A terminal clamp or a reduced speed
    // cap can brake faster; reset() on target changes or discontinuities.
    // Invalid/stale dt, disabled input, and invalid configuration clear state.
    Vec2 step(Vec2 error, double dt, const AimConfig& config) noexcept {
        const double distance = length(error);
        if (!config.enabled || !config.valid() || !finite(error) ||
            !std::isfinite(distance) || !std::isfinite(dt) || dt <= 0.0 ||
            dt > max_delta_time || config.strength == 0.0 || distance <= 1e-9) {
            reset();
            return {};
        }
        const double response = -std::expm1(-config.response_rate * config.strength * dt);
        const double desired_speed = std::min(config.max_speed_px_s,
                                               distance * (response / dt));
        const Vec2 direction{error.x / distance, error.y / distance};
        const Vec2 desired_velocity = direction * desired_speed;
        const Vec2 change = desired_velocity - velocity_;
        const double change_length = length(change);
        const double max_change = config.max_acceleration_px_s2 * dt;
        if (change_length > max_change) {
            velocity_ = velocity_ + change * (max_change / change_length);
        } else {
            velocity_ = desired_velocity;
        }
        // Configuration may have reduced the speed cap since the previous step.
        const double speed = length(velocity_);
        if (speed > config.max_speed_px_s) {
            velocity_ = velocity_ * (config.max_speed_px_s / speed);
        }
        Vec2 movement = velocity_ * dt;
        const double movement_length = length(movement);
        const double forward = movement.x * direction.x + movement.y * direction.y;
        if (!finite(movement) || !std::isfinite(movement_length) || forward <= 0.0) {
            reset();
            return {};
        }
        // Keep the step on the near side of the plane through the target and
        // prevent a lateral step from increasing the distance to the target.
        const double remaining_distance = length(error - movement);
        if (forward >= distance || movement_length >= distance) {
            movement = direction * std::min(distance, movement_length);
            velocity_ = movement * (1.0 / dt);
        } else if (!std::isfinite(remaining_distance) || remaining_distance > distance) {
            reset();
            return {};
        }
        return movement;
    }

private:
    Vec2 velocity_{};
};

} // namespace aim
