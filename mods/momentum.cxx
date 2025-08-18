// Created by moisrex on 8/17/25.

module;
#include <algorithm>
#include <chrono>
#include <cmath>
module foresight.mods.momentum;

using foresight::momentum_calculator;
using foresight::fsecs;
using foresight::velocity_tracker;

namespace {
    constexpr fsecs anim_dur{1.0};      // Default animation duration
    constexpr float   fps       = 60.0f;  // Target animation frame rate
    constexpr int     max_iters = 10;     // Max optimization iterations
    constexpr float   threshold = 0.001f; // Convergence threshold
    constexpr float   init_mag  = 1.1f;   // Initial curve magnitude
    constexpr float   min_prog  = 0.1f;   // Min initial progress
    constexpr float   max_prog  = 0.5f;   // Max initial progress

    /**
     * Project inertial movement distance
     *
     * Converts initial displacement to predicted travel distance
     * using empirical factor from real-world observations.
     *
     * Inertial Projection (Predicting Final Position):
     * This simple linear relationship converts an initial displacement (like a mouse wheel movement) into a
     * predicted travel distance:
     *   - factor = 16.7: An empirically measured constant derived from observing
     *     real-world scrolling behavior.
     *   - Why it works: On most platforms, the total scroll distance is roughly
     *     proportional to the initial input. Though real physics would involve deceleration, this linear
     *     approximation is sufficient for predicting where scrolling would naturally stop.
     */
    [[nodiscard]] float project_inertial(float const delta) noexcept {
        constexpr float factor = 16.7f; // Measured from platform behavior
        return factor * delta;
    }
} // namespace

void velocity_tracker::process_event(float const value, msecs const timestamp) noexcept {
    accumulated += value;
    if (last_timestamp.count() == 0) {
        last_timestamp = timestamp;
        return;
    }

    auto const  dt_duration = timestamp - last_timestamp;
    auto const  dt_us       = dt_duration.count();
    float const dt          = static_cast<float>(dt_us) * 1.0e-6f; // seconds

    if (dt < 1e-6f) {
        return;
    }

    // Use first-order IIR filter with time constant tau = 0.1 seconds
    // It's called First-order low-pass filter because:
    //   - "First-order" because it uses a simple differential equation of order one.
    //   - "Low-pass" because it "passes" low frequencies and "blocks" high ones.
    //   - "Filter" because it filters out unwanted noise or jitter.
    // Small τ → fast response, less smoothing
    // Large τ → slow response, more smoothing
    float const     v_instant = value / dt;
    constexpr float tau       = 0.1f; // 100ms smoothing
    float const     alpha     = 1.0f - std::exp(-dt / tau);

    // We use a discrete approximation called an exponential moving average (EMA)
    smoothed_velocity = alpha * v_instant + (1.0f - alpha) * smoothed_velocity;
    last_timestamp    = timestamp;
}

float velocity_tracker::velocity() const noexcept {
    return smoothed_velocity;
}

float velocity_tracker::get_recent_delta() const noexcept {
    return accumulated;
}

void velocity_tracker::reset() noexcept {
    accumulated       = 0.0f;
    smoothed_velocity = 0.0f;
    last_timestamp    = msecs::zero();
}

momentum_calculator::momentum_calculator(
  float const min_val,
  float const max_val,
  float const pos,
  float const delta,
  float const vel) noexcept
  : delta_(delta),
    vel_(vel),
    pos_(pos),
    min_(min_val),
    max_(max_val) {}

float momentum_calculator::pos_at(fsecs const time) noexcept {
    if (needs_init_) {
        init_curve();
        init_interp();
        needs_init_ = false;
    }

    float const progress = progress_at(time);
    return linear_only_ ? linear_pos_at(progress) : cubic_pos_at(progress);
}

fsecs momentum_calculator::duration() const noexcept {
    return anim_dur;
}

float momentum_calculator::pred_dest() const noexcept {
    // Calculate natural stopping point within bounds
    float const dest = pos_ + project_inertial(delta_);
    return std::clamp(dest, min_, max_);
}

void momentum_calculator::set_target(float const target) noexcept {
    target_ = target;
}

float momentum_calculator::linear_pos_at(float const progress) const noexcept {
    float const delta = target_.value() - pos_;
    return pos_ + progress * delta;
}

float momentum_calculator::cubic_pos_at(float const progress) const noexcept {
    float result = 0.0f;
    for (int i = 0; i < 4; ++i) {
        result += std::pow(progress, static_cast<float>(i)) * coeffs_[i];
    }
    return result;
}

void momentum_calculator::init_interp() noexcept {
    linear_only_ = true;

    // Skip complex interpolation for small movements
    if (std::abs(delta_) < 1.0f) {
        return;
    }

    // Check if we're already at target
    float const to_target      = target_.value() - pos_;
    float const to_target_dist = std::abs(to_target);
    if (to_target_dist < 0.001f) {
        return;
    }

    // Verify movement direction matches target direction
    float const delta_dir  = (delta_ > 0) ? 1.0f : -1.0f;
    float const target_dir = (to_target > 0) ? 1.0f : -1.0f;
    if (delta_dir != target_dir) {
        return;
    }

    // Calculate control points for natural motion curve
    float const side =
      to_target_dist / (2.0f * std::abs(delta_) / (std::abs(delta_) + to_target_dist) + 1.0f);

    float const ctrl1 = pos_ + side * delta_dir;
    float const ctrl2 = ctrl1 + side * target_dir;

    // Setup cubic Bezier coefficients
    coeffs_[0] = pos_;
    coeffs_[1] = 3.0f * (ctrl1 - pos_);
    coeffs_[2] = 3.0f * (pos_ - 2.0f * ctrl1 + ctrl2);
    coeffs_[3] = 3.0f * (ctrl1 - ctrl2) - pos_ + target_.value();

    linear_only_ = false;
}

void momentum_calculator::init_curve() noexcept {
    // Calculate initial progress from momentum
    float prog = min_prog;
    if (target_) {
        float const to_target = std::abs(target_.value() - pos_);
        if (to_target > 0.001f) {
            float const ratio = std::abs(delta_) / to_target;
            prog              = std::clamp(ratio, min_prog, max_prog);
        }
    }

    // Solve for curve parameters using iterative method
    float prev_decay = 1.0f;
    curve_mag_       = init_mag;

    for (int i = 0; i < max_iters; ++i) {
        decay_               = curve_mag_ / (curve_mag_ - prog);
        float const exponent = -fps * anim_dur.count();
        curve_mag_           = 1.0f / (1.0f - std::pow(decay_, exponent));

        if (std::abs(decay_ - prev_decay) < threshold) {
            break;
        }
        prev_decay = decay_;
    }
}

float momentum_calculator::progress_at(fsecs const time) const noexcept {
    // Normalize time to [0,1] range
    float const t = std::clamp(static_cast<float>(time.count() / anim_dur.count()), 0.0f, 1.0f);

    // Calculate progress using exponential decay curve
    float const exponent = -fps * static_cast<float>(anim_dur.count() * t);
    return std::min(1.0f, curve_mag_ * (1.0f - std::pow(decay_, exponent)));
}
