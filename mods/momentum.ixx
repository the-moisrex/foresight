module;
#include <chrono>
#include <cmath>
#include <optional>
export module foresight.mods.momentum;
import foresight.mods.context;

namespace foresight {

    export using fsecs = std::chrono::duration<double>;
    export using msecs = std::chrono::microseconds;

    /**
     * If the mouse suddenly moves fast, the filter will gradually ramp up to that speed.
     * If the mouse stops, it will gradually slow down.
     * Jitter or tiny movements get suppressed.
     */
    export struct velocity_tracker {
        void process_event(float value, msecs timestamp) noexcept;

        // Get final velocity when gesture ends (e.g., mouse release)
        [[nodiscard]] float velocity() const noexcept;

        // Get accumulated movement since last reset
        [[nodiscard]] float get_recent_delta() const noexcept;

        // Reset for a new gesture
        void reset() noexcept;

      private:
        float accumulated       = 0.0f;
        float smoothed_velocity = 0.0f;
        msecs last_timestamp    = msecs::zero();
    };

    /**
     * @class momentum_calculator
     * @brief Single-axis momentum animation calculator
     *
     * General-purpose implementation for:
     * - Scrolling systems
     * - Physics-based UI animations
     * - Any 1D momentum simulation
     *
     * Features:
     * - Predicts final position from initial velocity
     * - Creates natural animation curves
     * - Handles boundary constraints
     * - Supports dynamic target retargeting
     */
    export struct momentum_calculator {
        /**
         * Create momentum calculator
         * @param min_val Minimum valid position (e.g., start of scroll area)
         * @param max_val Maximum valid position (e.g., end of scroll area)
         * @param pos Current position when momentum starts
         * @param delta Initial displacement that created momentum
         * @param vel Initial velocity at momentum start
         */
        momentum_calculator(float min_val, float max_val, float pos, float delta, float vel) noexcept;
        momentum_calculator(momentum_calculator&&) noexcept        = default;
        momentum_calculator(momentum_calculator const&)            = default;
        momentum_calculator& operator=(momentum_calculator const&) = default;
        momentum_calculator& operator=(momentum_calculator&&)      = default;
        ~momentum_calculator() noexcept                            = default;

        /**
         * Get position at specific animation time
         * @param time Time elapsed since animation start
         * @return Calculated position
         */
        [[nodiscard]] float pos_at(fsecs time) noexcept;

        /**
         * Get total animation duration
         * @return Animation duration in seconds
         */
        [[nodiscard]] fsecs duration() const noexcept;

        /**
         * Predict final position without animation
         * Uses initial velocity to estimate natural stopping point.
         * @return Predicted destination
         */
        [[nodiscard]] float pred_dest() const noexcept;

        /**
         * Set new animation target
         * Used when target changes during animation (e.g., snap points).
         * @param target New target position
         */
        void set_target(float target) noexcept;


        /// Animation Curve Magnitude
        [[nodiscard]] float curve_magnitude() const noexcept {
            return curve_mag_;
        }

        /// Curve Decay Factor
        [[nodiscard]] float decay() const noexcept {
            return decay_;
        }

        [[nodiscard]] bool is_linear() const noexcept {
            return linear_only_;
        }

      private:
        /**
         * Initialize cubic interpolation coefficients
         * Creates Bezier curve for natural motion toward target.
         * Falls back to linear if conditions aren't suitable.
         */
        void init_interp() noexcept;

        /**
         * Setup animation timing curve
         * Configures exponential decay for realistic momentum feel.
         */
        void init_curve() noexcept;

        /**
         * Calculate animation progress at given time
         * @param time Current animation time
         * @return Progress fraction [0.0, 1.0]
         */
        [[nodiscard]] float progress_at(fsecs time) const noexcept;

        /**
         * Linear position interpolation
         * @param progress Current animation progress
         * @return Interpolated position
         */
        [[nodiscard]] float linear_pos_at(float progress) const noexcept;

        /**
         * Cubic position interpolation
         * @param progress Current animation progress
         * @return Interpolated position
         */
        [[nodiscard]] float cubic_pos_at(float progress) const noexcept;

        float                delta_;  // Initial displacement
        float                vel_;    // Initial velocity
        float                pos_;    // Starting position
        float                min_;    // Minimum valid position
        float                max_;    // Maximum valid position
        std::optional<float> target_; // Current animation target

        float curve_mag_{};           // Animation curve magnitude
        float decay_{};               // Curve decay factor
        float coeffs_[4]{};           // Cubic interpolation coefficients
        bool  linear_only_{};         // Force linear interpolation
        bool  needs_init_ = true;     // Needs initialization
    };

} // namespace foresight
