// Created by moisrex on 8/17/25.

module;
#include <array>
#include <chrono>
#include <cmath>
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

    // ── Momentum policy concept ────────────────────────────────────────────

    /**
     * A momentum policy defines *what* events to track and emit, and *how*
     * to transform input events during pass-through.
     *
     * Required static members:
     *   track_type, track_code_x, track_code_y  — velocity tracking codes
     *   emit_type, emit_code_x, emit_code_y     — animation emission codes
     *
     * Required static method:
     *   template <Context CtxT, typename MomentumT>
     *   static context_action pass_through(CtxT& ctx, MomentumT& momentum) noexcept;
     */
    template <typename T>
    concept MomentumPolicy = requires {
        { T::track_type } -> std::convertible_to<event_type::type_type>;
        { T::track_code_x } -> std::convertible_to<event_type::code_type>;
        { T::track_code_y } -> std::convertible_to<event_type::code_type>;
        { T::emit_type } -> std::convertible_to<event_type::type_type>;
        { T::emit_code_x } -> std::convertible_to<event_type::code_type>;
        { T::emit_code_y } -> std::convertible_to<event_type::code_type>;
    };

    // ── Policies ───────────────────────────────────────────────────────────

    /**
     * Scroll momentum policy: tracks scroll wheel events
     * (REL_WHEEL_HI_RES/REL_HWHEEL_HI_RES) and emits them during the
     * momentum animation to add inertial scrolling.
     *
     * Requires `scheduler` earlier in the pipeline to emit timed events.
     */
    export struct [[nodiscard]] scroll_momentum_policy {
        static constexpr auto track_type   = EV_REL;
        static constexpr auto track_code_x = REL_WHEEL_HI_RES;
        static constexpr auto track_code_y = REL_HWHEEL_HI_RES;
        static constexpr auto emit_type    = EV_REL;
        static constexpr auto emit_code_x  = REL_WHEEL_HI_RES;
        static constexpr auto emit_code_y  = REL_HWHEEL_HI_RES;

        /// Legacy scroll codes (quantized, ±1 per notch).
        static constexpr auto legacy_code_x = REL_HWHEEL;
        static constexpr auto legacy_code_y = REL_WHEEL;

        /// High-res multiplier: the hi-res value for one notch.
        static constexpr auto hi_res_per_notch = 120;

        /// Number of momentum events to schedule per axis.
        static constexpr int momentum_events = 30;
        /// Frame interval in ms (~60 fps).
        static constexpr int frame_ms        = 16;

        template <Context CtxT, typename MomentumT>
        static context_action pass_through(CtxT& ctx, MomentumT& momentum) noexcept {
            using enum context_action;

            static_assert(has_mod<basic_scheduler, CtxT>, "Add scheduler for the animations.");

            momentum.track(ctx.event());

            auto& sched = ctx.mod(scheduler);

            // Skip re-scheduling if we're mid-animation and this is a
            // self-emitted event (from the scheduler).  User scrolls (non-
            // self-emitted) can still re-trigger with updated velocity.
            if (momentum.is_animating() && sched.has_pending()
                && ctx.event().source() == device_id::self) {
                return next;
            }

            // Clear animation flag when the scheduler has drained.
            if (momentum.is_animating() && !sched.has_pending()) {
                momentum.clear_animating();
            }

            // Cancel any previously scheduled momentum.
            sched.cancel();

            // Compute momentum events from the current velocity.
            auto const [vel_x, vel_y] = momentum.current_velocity();
            if (std::abs(vel_x) < 0.5f && std::abs(vel_y) < 0.5f) {
                return next;
            }

            // Build a decaying sequence of scroll events on the stack.
            // todo: use inplace_vector
            // Up to 5 events per tick: hi-res x, legacy x, hi-res y, legacy y, syn
            static constexpr int               max_events = momentum_events * 5;
            std::array<event_type, max_events> events{};
            std::size_t                        count = 0;

            // Accumulate fractional hi-res movement and emit ±1 legacy events
            // each time the accumulator crosses a notch boundary (120 units).
            auto acc_x = 0.0f;
            auto acc_y = 0.0f;

            for (int i = 0; i < momentum_events; ++i) {
                float const factor = momentum.decay_factor(i);
                auto const  dx     = static_cast<event_type::value_type>(vel_x * factor);
                auto const  dy     = static_cast<event_type::value_type>(vel_y * factor);

                if (dx != 0) {
                    // Hi-res (non-quantized)
                    events[count++] = event_type(emit_type, emit_code_x, dx);
                    // Legacy (quantized): accumulate and emit ±1 per notch.
                    acc_x += static_cast<float>(dx);
                    while (acc_x >= hi_res_per_notch) {
                        events[count++] = event_type(emit_type, legacy_code_x, 1);
                        acc_x -= hi_res_per_notch;
                    }
                    while (acc_x <= -hi_res_per_notch) {
                        events[count++] = event_type(emit_type, legacy_code_x, -1);
                        acc_x += hi_res_per_notch;
                    }
                }
                if (dy != 0) {
                    // Hi-res (non-quantized)
                    events[count++] = event_type(emit_type, emit_code_y, dy);
                    // Legacy (quantized): accumulate and emit ±1 per notch.
                    acc_y += static_cast<float>(dy);
                    while (acc_y >= hi_res_per_notch) {
                        events[count++] = event_type(emit_type, legacy_code_y, 1);
                        acc_y -= hi_res_per_notch;
                    }
                    while (acc_y <= -hi_res_per_notch) {
                        events[count++] = event_type(emit_type, legacy_code_y, -1);
                        acc_y += hi_res_per_notch;
                    }
                }
                events[count++] = syn();
            }

            if (count > 0) {
                sched.schedule(std::span{events.data(), count}, frame_ms);
                momentum.set_animating();
            }

            return next;
        }
    };

    // ── Momentum engine ────────────────────────────────────────────────────

    /**
     * Non-template base class that owns the velocity trackers, animation
     * calculators, and all non-template-dependent logic.  Defined out-of-line
     * in the .cxx so the pimpl impl stays hidden.
     *
     * `basic_momentum<Policy>` inherits from this, adding only the
     * policy-provided constants and the template `operator()`.
     */
    export struct [[nodiscard]] basic_momentum_base : pimpl_idiom<basic_momentum_base> {
        using pimpl_idiom::pimpl_idiom;
        using value_type = event_type::value_type;

        void track(event_type const& event) noexcept;

        [[nodiscard]] bool is_active() const noexcept;

        /// Get the smoothed velocity for both axes.
        [[nodiscard]] std::pair<float, float> current_velocity() const noexcept;

        /// Compute a decay factor for the i-th momentum event.
        [[nodiscard]] float decay_factor(int i) const noexcept;

        /// Whether a momentum animation is currently in progress.
        [[nodiscard]] bool is_animating() const noexcept;

        /// Mark the animation as in progress.
        void set_animating() noexcept;

        /// Mark the animation as finished.
        void clear_animating() noexcept;

        context_action operator()(start_tag) noexcept;

      protected:
        /// Configure which event codes to track.
        /// Called by derived classes in their start_tag handler.
        void configure(event_type::type_type track_type, event_type::code_type track_code_x, event_type::code_type track_code_y) noexcept;
    };

    /**
     * Generic momentum pipeline mod, parameterized on a Policy.
     *
     * Tracks the velocity of scroll events and schedules momentum events
     * via the `scheduler` mod to add inertial scrolling. The policy
     * defines what to track, what to emit, and how to transform events.
     *
     * @par Example
     * @code
     *   // Scroll momentum:
     *   auto pipeline = context
     *       | io_manager
     *       | input_manager
     *       | intercept[keyboard, mouse]
     *       | scheduler
     *       | momentum_scroll
     *       | output;
     * @endcode
     */
    export template <MomentumPolicy Policy>
    struct [[nodiscard]] basic_momentum : basic_momentum_base {
        using basic_momentum_base::basic_momentum_base;

        static constexpr auto track_type   = Policy::track_type;
        static constexpr auto track_code_x = Policy::track_code_x;
        static constexpr auto track_code_y = Policy::track_code_y;
        static constexpr auto emit_type    = Policy::emit_type;
        static constexpr auto emit_code_x  = Policy::emit_code_x;
        static constexpr auto emit_code_y  = Policy::emit_code_y;

        /// Pipeline mod: delegate pass-through behavior to the policy.
        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            return Policy::pass_through(ctx, *this);
        }

        /// Initialize state on pipeline start.
        context_action operator()(start_tag) noexcept {
            configure(track_type, track_code_x, track_code_y);
            return this->basic_momentum_base::operator()(start_tag{});
        }
    };

    // ── Convenience aliases ────────────────────────────────────────────────

    /// Scroll momentum: tracks and emits REL_WHEEL_HI_RES during animation.
    export constexpr auto momentum_scroll = basic_momentum<scroll_momentum_policy>{};

} // namespace fs8
