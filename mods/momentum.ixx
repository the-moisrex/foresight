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

    // ── Momentum config ───────────────────────────────────────────────────

    export struct [[nodiscard]] momentum_config {
        /// Number of momentum frames to schedule per axis.
        int momentum_frames      = 30;
        /// Frame interval in ms (~60 fps).
        int frame_ms             = 16;
        /// Per-frame velocity scale: delta = velocity * scale * decay^step.
        float initial_scale      = 0.005f;
        /// Decay rate per frame.  Closer to 1.0 = longer tail.
        float decay_rate         = 0.93f;
        /// How much an opposing scroll event reduces the current momentum velocity.
        /// 0.0 = opposing scrolls have no effect, 1.0 = a single opposing notch
        /// cancels all momentum in that axis.
        float reversal_scale     = 0.3f;
        /// Maximum mouse travel (in accumulated REL units) from the point
        /// where momentum started.  A value of 0 disables distance-based
        /// slowdown.  Both axes are tracked independently; momentum scales
        /// down when the furthest axis exceeds this threshold.
        float max_mouse_distance = 500.0f;
    };

    // ── Momentum engine ────────────────────────────────────────────────────

    /**
     * Non-template base class that owns the velocity trackers, animation
     * calculators, and all non-template-dependent logic.  Defined out-of-line
     * in the .cxx so the pimpl impl stays hidden.
     *
     * `basic_momentum_scroll` inherits from this, adding only the template `operator()`.
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

        /// Record the current mouse position as the origin for distance tracking.
        void set_mouse_origin() noexcept;

        /// Update mouse position during animation and track distance from origin.
        void update_mouse_distance(event_type const& event) noexcept;

        /// Returns a scale factor [0,1] based on how far the mouse has moved
        /// from the origin.  1.0 = at origin, 0.0 = at or past max_distance.
        [[nodiscard]] float mouse_distance_scale() const noexcept;

        /// Whether distance tracking is enabled (max_mouse_distance > 0).
        [[nodiscard]] bool has_distance_tracking() const noexcept;

        /// Cancel the pending momentum tick.
        void cancel_momentum_tick() noexcept;

        context_action operator()(start_tag) noexcept;
    };

    // ── Momentum mod ─────────────────────────────────────────────────────

    /**
     * Scroll momentum pipeline mod.
     *
     * Tracks the velocity of scroll events and schedules momentum events
     * via the `scheduler` mod to add inertial scrolling. Emits
     * REL_WHEEL_HI_RES / REL_HWHEEL_HI_RES events with legacy fallback.
     *
     * Distance-based slowdown is enabled by default (max_mouse_distance = 500).
     * If the mouse moves far enough from where momentum started, the momentum
     * scales down to zero.  Moving the mouse back toward the origin restores
     * momentum.
     *
     * Use `operator[]` to configure:
     *   momentum_scroll[40]           — 40 momentum frames
     *   momentum_scroll[30, 16]       — 30 frames at 16ms interval
     *   momentum_scroll[30, 16, 800]  — + max mouse distance 800
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

      private:
        momentum_config config;

      public:
        /// Set the number of momentum frames.
        consteval basic_momentum_scroll operator[](int const frames) const noexcept {
            basic_momentum_scroll result{*this};
            result.config.momentum_frames = frames;
            return result;
        }

        /// Set the number of momentum frames and frame interval.
        consteval basic_momentum_scroll operator[](int const frames, int const frame_ms) const noexcept {
            basic_momentum_scroll result{*this};
            result.config.momentum_frames = frames;
            result.config.frame_ms        = frame_ms;
            return result;
        }

        /// Set the number of momentum frames, frame interval, and max mouse distance.
        consteval basic_momentum_scroll operator[](int const frames, int const frame_ms, float const max_dist) const noexcept {
            basic_momentum_scroll result{*this};
            result.config.momentum_frames    = frames;
            result.config.frame_ms           = frame_ms;
            result.config.max_mouse_distance = max_dist;
            return result;
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;
            auto const& event = ctx.event();

            if (event.source() == device_id::scheduler) {
                return next;
            }

            if (is_high_res_scroll(event)) {
                return handle_scroll_event(ctx.mod(scheduler), event);
            }

            // Mouse movements during animation: track distance from origin.
            // The distance scale naturally reduces momentum to zero.
            if (is_mouse_movement(event) && is_animating()) {
                update_mouse_distance(event);
                return next;
            }

            return next;
        }

        template <Context CtxT>
        context_action operator()(CtxT& /*ctx*/, start_tag) noexcept {
            static_assert(has_mod<basic_scheduler, CtxT>, "Add scheduler for the animations.");
            auto const result = this->basic_momentum_base::operator()(start_tag{});
            set_max_mouse_distance(config.max_mouse_distance);
            return result;
        }

        /// Handle a scroll event: track velocity and schedule momentum if sufficient.
        context_action handle_scroll_event(basic_scheduler& sched, event_type const& event) noexcept;
    };

    /// Scroll momentum: tracks and emits REL_WHEEL_HI_RES during animation.
    /// Distance-based slowdown is enabled by default (max_mouse_distance = 500).
    export constexpr auto momentum_scroll = basic_momentum_scroll{};

} // namespace fs8
