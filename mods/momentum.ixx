// Created by moisrex on 8/17/25.

module;
#include <array>
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

    // ── Momentum policy concept ────────────────────────────────────────────

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

    export struct [[nodiscard]] scroll_momentum_policy {
        static constexpr auto track_type   = EV_REL;
        static constexpr auto track_code_x = REL_WHEEL_HI_RES;
        static constexpr auto track_code_y = REL_HWHEEL_HI_RES;
        static constexpr auto emit_type    = EV_REL;
        static constexpr auto emit_code_x  = REL_WHEEL_HI_RES;
        static constexpr auto emit_code_y  = REL_HWHEEL_HI_RES;

        /// Legacy scroll codes (quantized, +/-1 per notch).
        static constexpr auto legacy_code_x = REL_HWHEEL;
        static constexpr auto legacy_code_y = REL_WHEEL;

        /// High-res multiplier: the hi-res value for one notch.
        static constexpr auto hi_res_per_notch = 120;

        /// Number of momentum frames to schedule per axis.
        static constexpr int momentum_events = 30;
        /// Frame interval in ms (~60 fps).
        static constexpr int frame_ms        = 16;
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

    // ── Momentum tick state ──────────────────────────────────────────────

    template <MomentumPolicy Policy>
    struct momentum_tick_state {
        float vel_x          = 0.0f;
        float vel_y          = 0.0f;
        int   step           = 0;
        float distance_scale = 1.0f;
        float acc_x          = 0.0f;
        float acc_y          = 0.0f;
    };

    // ── Momentum context (passed as tick_fn data) ─────────────────────────

    template <MomentumPolicy Policy>
    struct momentum_context {
        basic_scheduler&             scheduler;
        basic_scheduler::tick_handle handle{};
        momentum_tick_state<Policy>  tick_state{};
        basic_momentum_base*         momentum_base = nullptr;
        /// Buffer for events produced by the tick callback.
        std::array<event_type, 8> event_buffer{};
    };

    // ── Momentum tick callback ────────────────────────────────────────────

    /**
     * Tick callback invoked by the scheduler on each timer tick.
     * Produces ALL scroll events for one frame and returns them as a span.
     *
     * Events per frame:
     *   hi_res_x (if dx != 0) + legacy_x events + hi_res_y (if dy != 0) + legacy_y events + syn_report
     */
    template <MomentumPolicy Policy>
    basic_scheduler::tick_result momentum_tick(void* const data) noexcept {
        using enum context_action;

        auto& ctx   = *static_cast<momentum_context<Policy>*>(data);
        auto& state = ctx.tick_state;

        if (state.step >= Policy::momentum_events) {
            return {};
        }

        // Recompute distance scale from live mouse distance tracking.
        if (ctx.momentum_base && ctx.momentum_base->has_distance_tracking()) {
            state.distance_scale = ctx.momentum_base->mouse_distance_scale();
        }

        // Compute per-frame delta (with decay and distance scaling).
        float const factor = 0.005f * std::pow(0.93f, static_cast<float>(state.step));
        auto const  dx     = static_cast<float>(state.vel_x * factor * state.distance_scale);
        auto const  dy     = static_cast<float>(state.vel_y * factor * state.distance_scale);

        std::size_t count = 0;

        // ── hi_res_x ──────────────────────────────────────────────────
        if (dx != 0.0f) {
            ctx.event_buffer[count++]  = event_type(Policy::emit_type, Policy::emit_code_x, static_cast<event_type::value_type>(dx));
            state.acc_x               += dx;
        }

        // ── legacy_x ──────────────────────────────────────────────────
        {
            auto const notch = static_cast<float>(Policy::hi_res_per_notch);
            while (state.acc_x >= notch) {
                state.acc_x               -= notch;
                ctx.event_buffer[count++]  = event_type(Policy::emit_type, Policy::legacy_code_x, 1);
            }
            while (state.acc_x <= -notch) {
                state.acc_x               += notch;
                ctx.event_buffer[count++]  = event_type(Policy::emit_type, Policy::legacy_code_x, -1);
            }
        }

        // ── hi_res_y ──────────────────────────────────────────────────
        if (dy != 0.0f) {
            ctx.event_buffer[count++]  = event_type(Policy::emit_type, Policy::emit_code_y, static_cast<event_type::value_type>(dy));
            state.acc_y               += dy;
        }

        // ── legacy_y ──────────────────────────────────────────────────
        {
            auto const notch = static_cast<float>(Policy::hi_res_per_notch);
            while (state.acc_y >= notch) {
                state.acc_y               -= notch;
                ctx.event_buffer[count++]  = event_type(Policy::emit_type, Policy::legacy_code_y, 1);
            }
            while (state.acc_y <= -notch) {
                state.acc_y               += notch;
                ctx.event_buffer[count++]  = event_type(Policy::emit_type, Policy::legacy_code_y, -1);
            }
        }

        // ── SYN_REPORT ────────────────────────────────────────────────
        if (count > 0) {
            ctx.event_buffer[count++] = syn();
        }

        ++state.step;

        if (state.step >= Policy::momentum_events) {
            ctx.momentum_base->clear_animating();
            ctx.momentum_base->reset_mouse_distance();
            return {
              .events       = std::span<event_type const>{ctx.event_buffer.data(), count},
              .next_timeout = basic_scheduler::cancel_tick,
            };
        }

        return {
          .events       = std::span<event_type const>{ctx.event_buffer.data(), count},
          .next_timeout = std::chrono::microseconds{Policy::frame_ms * 1000},
        };
    }

    // ── Momentum mod ─────────────────────────────────────────────────────

    /**
     * Generic momentum pipeline mod, parameterized on a Policy.
     *
     * Tracks the velocity of scroll events and schedules momentum events
     * via the `scheduler` mod to add inertial scrolling. The policy
     * defines what to track, what to emit, and how to transform events.
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
    export template <MomentumPolicy Policy>
    struct [[nodiscard]] basic_momentum : basic_momentum_base {
        using basic_momentum_base::basic_momentum_base;

        static constexpr auto track_type   = Policy::track_type;
        static constexpr auto track_code_x = Policy::track_code_x;
        static constexpr auto track_code_y = Policy::track_code_y;
        static constexpr auto emit_type    = Policy::emit_type;
        static constexpr auto emit_code_x  = Policy::emit_code_x;
        static constexpr auto emit_code_y  = Policy::emit_code_y;

        /// Maximum mouse travel (in accumulated REL units) before momentum is
        /// fully cancelled.  A value of 0 disables distance-based slowdown.
        float max_mouse_distance = 0.0f;

        /// Set the number of momentum events (default from policy).
        consteval basic_momentum operator[](int const events) const noexcept {
            basic_momentum result{*this};
            result.num_events_ = events;
            return result;
        }

        /// Track velocity from events and schedule momentum when velocity
        /// is sufficient.  Scheduler-emitted events are passed through.
        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;
            auto const& event = ctx.event();

            // Scheduler-emitted events: pass through without tracking.
            if (event.source() == device_id::scheduler) {
                return next;
            }

            // Track velocity for hi_res scroll events only.
            bool const is_scroll_hi_res =
              event.is(Policy::track_type, Policy::track_code_x) || event.is(Policy::track_type, Policy::track_code_y);

            if (is_scroll_hi_res) {
                track(event);
                reset_mouse_distance();

                auto const [vel_x, vel_y] = current_velocity();
                if (std::abs(vel_x) < 0.5f && std::abs(vel_y) < 0.5f) {
                    return next;
                }

                // Cancel any previous momentum tick, then schedule a fresh one.
                static momentum_context<Policy> mctx{ctx.mod(scheduler), {}, {}, this};
                mctx.scheduler.cancel(mctx.handle);
                mctx.tick_state                = {};
                mctx.tick_state.vel_x          = vel_x;
                mctx.tick_state.vel_y          = vel_y;
                mctx.tick_state.distance_scale = 1.0f;
                mctx.handle                    = mctx.scheduler.schedule(
                  [](void* const d) noexcept -> basic_scheduler::tick_result {
                      return momentum_tick<Policy>(d);
                  },
                  &mctx,
                  std::chrono::microseconds{Policy::frame_ms * 1000});
                set_animating();
            }

            // Mouse movements during animation: accumulate distance if tracking,
            // or cancel momentum if distance tracking is disabled.
            if (is_mouse_movement(event) && is_animating()) {
                if (has_distance_tracking()) {
                    accumulate_mouse_distance(event);
                    return next;
                }
                // No distance tracking — mouse move cancels momentum.
                ctx.mod(scheduler).cancel_all();
                clear_animating();
                reset_mouse_distance();
                return next;
            }

            return next;
        }

        /// Initialize state on pipeline start.
        template <Context CtxT>
        context_action operator()(CtxT& /*ctx*/, start_tag) noexcept {
            static_assert(has_mod<basic_scheduler, CtxT>, "Add scheduler for the animations.");
            configure(track_type, track_code_x, track_code_y);
            auto const result = this->basic_momentum_base::operator()(start_tag{});
            set_max_mouse_distance(max_mouse_distance);
            return result;
        }

      private:
        int num_events_ = Policy::momentum_events;
        int frame_ms_   = Policy::frame_ms;
    };

    // ── Convenience aliases ────────────────────────────────────────────────

    /// Scroll momentum: tracks and emits REL_WHEEL_HI_RES during animation.
    /// Set `max_mouse_distance` to enable distance-based slowdown on mouse moves.
    export constexpr auto momentum_scroll = basic_momentum<scroll_momentum_policy>{};

} // namespace fs8
