// Created by moisrex on 8/17/25.

module;
#include <chrono>
#include <cmath>
module foresight.mods.momentum;

using foresight::fsecs;
using foresight::momentum_calculator;
using foresight::velocity_tracker;

namespace {
    constexpr fsecs anim_dur{1.0};      // Default animation duration
    constexpr float fps       = 60.0f;  // Target animation frame rate
    constexpr int   max_iters = 10;     // Max optimization iterations
    constexpr float threshold = 0.001f; // Convergence threshold
    constexpr float init_mag  = 1.1f;   // Initial curve magnitude
    constexpr float min_prog  = 0.1f;   // Min initial progress
    constexpr float max_prog  = 0.5f;   // Max initial progress

    /**
     * Project inertial movement distance
     *
     * Converts initial displacement to predicted travel distance
     * using an empirical factor from real-world observations.
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
    // Accumulate total movement for delta calculation
    accumulated += value;

    // Initialize timestamp on first event
    if (last_timestamp.count() == 0) {
        last_timestamp = timestamp;
        return;
    }

    // Calculate time delta in seconds
    auto const  dt_duration = timestamp - last_timestamp;
    auto const  dt_us       = dt_duration.count();
    float const dt          = static_cast<float>(dt_us) * 1.0e-6f; // Convert microseconds to seconds

    // Ignore if time delta is too small to avoid division by zero or numerical instability
    if (dt < 1e-6f) {
        return;
    }

    // Calculate instantaneous velocity (units per second)
    float const v_instant = value / dt;

    // Apply first-order IIR low-pass filter with time constant tau = 0.1 seconds
    // This smooths out jitter while preserving the overall motion trend
    //
    // First-order low-pass filter explanation:
    //   - "First-order" because it uses a simple differential equation of order one
    //   - "Low-pass" because it "passes" low frequencies and "blocks" high ones
    //   - "Filter" because it filters out unwanted noise or jitter
    //
    // Time constant effects:
    //   - Small τ (e.g., 0.01s) → fast response, less smoothing
    //   - Large τ (e.g., 1.0s) → slow response, more smoothing
    constexpr float tau   = 0.1f;                       // 100ms time constant for smoothing
    float const     alpha = 1.0f - std::exp(-dt / tau); // Filter coefficient

    // Apply exponential moving average (EMA) for velocity smoothing
    // This provides a weighted average that emphasizes recent values while
    // maintaining continuity with previous measurements
    smoothed_velocity = alpha * v_instant + (1.0f - alpha) * smoothed_velocity;
    last_timestamp    = timestamp;
}

float velocity_tracker::velocity() const noexcept {
    // Return the smoothed velocity calculated by the low-pass filter
    return smoothed_velocity;
}

float velocity_tracker::get_recent_delta() const noexcept {
    // Return accumulated movement since last reset
    return accumulated;
}

void velocity_tracker::reset() noexcept {
    // Reset all tracking variables to initial state
    accumulated       = 0.0f;          // Clear accumulated movement
    smoothed_velocity = 0.0f;          // Reset smoothed velocity
    last_timestamp    = msecs::zero(); // Clear timestamp
}

momentum_calculator::momentum_calculator(
  float const pos,
  float const delta,
  float const vel) noexcept
  : delta_(delta), // Initial displacement that created momentum
    vel_(vel),     // Initial velocity at momentum start
    pos_(pos),     // Starting position when momentum begins
    target_{pred_dest()} {
    init_curve();
    init_interp();
}

float momentum_calculator::pos_at(fsecs const time) noexcept {
    float const progress = progress_at(time);
    return linear_only_ ? linear_pos_at(progress) : cubic_pos_at(progress);
}

fsecs momentum_calculator::duration() const noexcept {
    // Return fixed animation duration
    return anim_dur;
}

float momentum_calculator::pred_dest() const noexcept {
    // Calculate natural stopping point and clamp within bounds
    return pos_ + project_inertial(delta_);
}

void momentum_calculator::set_target(float const target) noexcept {
    // Set new animation target for dynamic retargeting
    target_ = target;
}

float momentum_calculator::linear_pos_at(float const progress) const noexcept {
    // Simple linear interpolation between start and target positions
    float const delta = target_ - pos_;
    return pos_ + progress * delta;
}

float momentum_calculator::cubic_pos_at(float const progress) const noexcept {
    // Evaluate cubic Bezier curve at given progress
    // Formula: B(t) = P0*(1-t)³ + 3*P1*t*(1-t)² + 3*P2*t²*(1-t) + P3*t³
    // Which expands to: B(t) = P0 + (P1-P0)*3*t + (P0-2*P1+P2)*3*t² + (P1-P2)*3*t³ - P0*t³ + P3*t³
    // The coefficients are precomputed in init_interp()
    float result = 0.0f;
    for (int i = 0; i < 4; ++i) {
        result += std::pow(progress, static_cast<float>(i)) * coeffs_[i];
    }
    return result;
}

void momentum_calculator::init_interp() noexcept {
    // Default to linear interpolation
    linear_only_ = true;

    // Skip complex interpolation for negligible movements
    if (std::abs(delta_) < 1.0f) {
        return;
    }

    // Check if we're already at target (within tolerance)
    float const to_target      = target_ - pos_;
    float const to_target_dist = std::abs(to_target);
    if (to_target_dist < 0.001f) {
        return;
    }

    // Verify movement direction matches target direction to ensure natural motion
    float const delta_dir  = (delta_ > 0) ? 1.0f : -1.0f;    // Direction of initial movement
    float const target_dir = (to_target > 0) ? 1.0f : -1.0f; // Direction to target
    if (delta_dir != target_dir) {
        return;
    }

    // Calculate control points for a natural cubic Bezier curve
    // This creates an "S-curve" motion that starts in the direction of initial momentum
    // and smoothly transitions to the target

    // Calculate side length for control points using a weighted average approach
    // This ensures the curve has appropriate curvature based on initial momentum and distance to target
    float const side =
      to_target_dist / (2.0f * std::abs(delta_) / (std::abs(delta_) + to_target_dist) + 1.0f);

    // Control points for the cubic Bezier curve
    float const ctrl1 = pos_ + side * delta_dir;   // First control point in momentum direction
    float const ctrl2 = ctrl1 + side * target_dir; // Second control point toward target

    // Precompute Bezier coefficients for efficient evaluation
    // These coefficients transform the Bezier calculation into a polynomial form
    coeffs_[0] = pos_;                                    // P0 coefficient
    coeffs_[1] = 3.0f * (ctrl1 - pos_);                   // P1 coefficient
    coeffs_[2] = 3.0f * (pos_ - 2.0f * ctrl1 + ctrl2);    // P2 coefficient
    coeffs_[3] = 3.0f * (ctrl1 - ctrl2) - pos_ + target_; // P3 coefficient

    // Enable cubic interpolation
    linear_only_ = false;
}

void momentum_calculator::init_curve() noexcept {
    // Calculate initial progress based on momentum characteristics
    // This determines how much of the animation curve is already "completed" by initial velocity
    float prog = min_prog; // Default to minimum progress

    // If we have a target, calculate progress ratio based on initial momentum vs. distance to target
    float const to_target = std::abs(target_ - pos_);
    if (to_target > 0.001f) {
        // Ratio of initial momentum to distance to target, clamped to reasonable bounds
        float const ratio = std::abs(delta_) / to_target;
        prog              = std::clamp(ratio, min_prog, max_prog);
    }

    // Solve for curve parameters using iterative optimization
    // This configures an exponential decay function that feels natural for momentum
    float prev_decay = 1.0f;     // Previous decay factor for convergence check
    curve_mag_       = init_mag; // Initial curve magnitude

    // Iteratively solve for decay factor and curve magnitude
    // The goal is to find parameters that satisfy our constraints:
    // 1. s(0) = 0 (start of animation)
    // 2. s(1) = 1 (end of animation)
    // 3. s(1/k) = prog (initial progress)
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
    // Normalize time to [0,1] range for animation progress calculation
    float const t = std::clamp(static_cast<float>(time.count() / anim_dur.count()), 0.0f, 1.0f);

    // Calculate progress using exponential decay curve for natural momentum feel
    // Formula: s(t) = A * (1 - b^(-k*t)) where:
    //   - A = curve_mag_ (curve magnitude)
    //   - b = decay_ (decay factor)
    //   - k = fps * anim_dur.count() (total frames in animation)
    //   - t = normalized time
    float const exponent = -fps * static_cast<float>(anim_dur.count() * t);
    return std::min(1.0f, curve_mag_ * (1.0f - std::pow(decay_, exponent)));
}
