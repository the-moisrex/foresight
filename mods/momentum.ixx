// Created by moisrex on 8/17/25.

module;
#include <chrono>
#include <cmath>
#include <cstddef>
#include <linux/input-event-codes.h>
export module fs8.mods:momentum;
import fs8.context;
import fs8.pimpl;
import fs8.event;
import :scheduler;

namespace fs8 {

    export using fsecs = std::chrono::duration<double>;
    export using msecs = std::chrono::microseconds;
    export using usecs = std::chrono::microseconds;

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
        momentum_calculator(float pos, float delta, float vel) noexcept;
        momentum_calculator(momentum_calculator&&) noexcept        = default;
        momentum_calculator(momentum_calculator const&)            = default;
        momentum_calculator& operator=(momentum_calculator const&) = default;
        momentum_calculator& operator=(momentum_calculator&&)      = default;
        ~momentum_calculator() noexcept                            = default;

        [[nodiscard]] float pos_at(fsecs time) const noexcept;
        [[nodiscard]] fsecs duration() const noexcept;
        [[nodiscard]] float pred_dest() const noexcept;
        void                set_target(float target) noexcept;

        [[nodiscard]] float curve_magnitude() const noexcept {
            return curve_mag_;
        }

        [[nodiscard]] float decay() const noexcept {
            return decay_;
        }

        [[nodiscard]] bool is_linear() const noexcept {
            return linear_only_;
        }

      private:
        void                init_interp() noexcept;
        void                init_curve() noexcept;
        [[nodiscard]] float progress_at(fsecs time) const noexcept;
        [[nodiscard]] float linear_pos_at(float progress) const noexcept;
        [[nodiscard]] float cubic_pos_at(float progress) const noexcept;

        float delta_;
        float vel_;
        float pos_;
        float target_;
        float curve_mag_{};
        float decay_{};
        float coeffs_[4]{};
        bool  linear_only_{};
    };

    // ── Momentum engine ────────────────────────────────────────────────────

    /**
     * Non-template base class that owns the velocity trackers, animation
     * calculators, and all non-template-dependent logic.  Defined out-of-line
     * in the .cxx so the pimpl impl stays hidden.
     *
     * `momentum_scroll` inherits from this, adding only the template `operator()`.
     */
    export struct [[nodiscard]] basic_momentum_base : pimpl_idiom<basic_momentum_base> {
        using pimpl_idiom::pimpl_idiom;
        using value_type = event_type::value_type;

        void track(event_type const& event) noexcept;

        [[nodiscard]] bool is_active() const noexcept;

        /// Get the smoothed velocity for both axes.
        [[nodiscard]] std::pair<float, float> current_velocity() const noexcept;

        /// Whether a momentum animation is currently in progress.
        [[nodiscard]] bool is_animating() const noexcept;

        /// Mark the animation as in progress.
        void set_animating() noexcept;

        /// Mark the animation as finished.
        void clear_animating() noexcept;

        /// Set the max mouse distance before momentum is fully cancelled.
        /// A value of 0 disables distance-based slowdown.
        void set_max_mouse_distance(float d) noexcept;

        /// Accumulate mouse movement distance (from REL_X/REL_Y events).
        void accumulate_mouse_distance(event_type const& event) noexcept;

        /// Reset accumulated mouse distance to zero.
        void reset_mouse_distance() noexcept;

        /// Returns a scale factor [0,1] based on how far the mouse has moved.
        /// 1.0 = no movement, 0.0 = at or past max_distance.
        [[nodiscard]] float mouse_distance_scale() const noexcept;

        /// Whether distance tracking is enabled (max_mouse_distance > 0).
        [[nodiscard]] bool has_distance_tracking() const noexcept;

        context_action operator()(start_tag) noexcept;

      protected:
        /// Configure which event codes to track.
        void configure(event_type::type_type track_type, event_type::code_type track_code_x, event_type::code_type track_code_y) noexcept;
    };

    // ── Momentum mod ─────────────────────────────────────────────────────

    /**
     * Scroll momentum pipeline mod.
     *
     * Tracks the velocity of scroll events and schedules momentum events
     * via the `scheduler` mod to add inertial scrolling. Emits
     * REL_WHEEL_HI_RES / REL_HWHEEL_HI_RES events with legacy fallback.
     *
     * Set `max_mouse_distance` to enable distance-based slowdown on mouse moves.
     *
     * @par Example
     * @code
     *   auto pipeline = context
     *       | io_manager
     *       | input_manager
     *       | intercept[keyboard, mouse]
     *       | scheduler
     *       | momentum_scroll
     *       | output;
     * @endcode
     */
    export struct [[nodiscard]] basic_momentum_scroll : basic_momentum_base {
        using basic_momentum_base::basic_momentum_base;

        static constexpr auto track_type   = EV_REL;
        static constexpr auto track_code_x = REL_WHEEL_HI_RES;
        static constexpr auto track_code_y = REL_HWHEEL_HI_RES;
        static constexpr auto emit_type    = EV_REL;
        static constexpr auto emit_code_x  = REL_WHEEL_HI_RES;
        static constexpr auto emit_code_y  = REL_HWHEEL_HI_RES;

        /// Legacy scroll codes (quantized, +/-1 per notch).
        static constexpr auto legacy_code_x = REL_HWHEEL;
        static constexpr auto legacy_code_y = REL_HWHEEL;

        /// High-res multiplier: the hi-res value for one notch.
        static constexpr auto hi_res_per_notch = 120;

        /// Number of momentum frames to schedule per axis.
        static constexpr int momentum_events = 30;
        /// Frame interval in ms (~60 fps).
        static constexpr int frame_ms        = 16;

        /// Maximum mouse travel (in accumulated REL units) before momentum is
        /// fully cancelled.  A value of 0 disables distance-based slowdown.
        float max_mouse_distance = 0.0f;

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;
            auto const& event = ctx.event();

            if (event.source() == device_id::scheduler) {
                return next;
            }

            bool const is_scroll =
              event.is(track_type, track_code_x) || event.is(track_type, track_code_y);

            if (is_scroll) {
                return handle_scroll_event(ctx.mod(scheduler), event);
            }

            // Mouse movements during animation: accumulate distance if tracking,
            // or cancel momentum if distance tracking is disabled.
            if (is_mouse_movement(event) && is_animating()) {
                if (has_distance_tracking()) {
                    accumulate_mouse_distance(event);
                    return next;
                }
                ctx.mod(scheduler).cancel_all();
                clear_animating();
                reset_mouse_distance();
                return next;
            }

            return next;
        }

        template <Context CtxT>
        context_action operator()(CtxT& /*ctx*/, start_tag) noexcept {
            static_assert(has_mod<basic_scheduler, CtxT>, "Add scheduler for the animations.");
            configure(track_type, track_code_x, track_code_y);
            auto const result = this->basic_momentum_base::operator()(start_tag{});
            set_max_mouse_distance(max_mouse_distance);
            return result;
        }

        /// Handle a scroll event: track velocity and schedule momentum if sufficient.
        context_action handle_scroll_event(basic_scheduler& sched, event_type const& event) noexcept;
    };

    /// Scroll momentum: tracks and emits REL_WHEEL_HI_RES during animation.
    /// Set `max_mouse_distance` to enable distance-based slowdown on mouse moves.
    export constexpr auto momentum_scroll = basic_momentum_scroll{};

} // namespace fs8
