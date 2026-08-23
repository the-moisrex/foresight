// Created by moisrex on 8/17/25.

module;
#include <chrono>
#include <cmath>
#include <linux/input-event-codes.h>
#include <optional>
#include <thread>
export module fs8.mods:momentum;
import fs8.context;
import fs8.pimpl;
import fs8.event;
import :quantifier;

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
     * Default no-op policy: tracks and emits the same event codes.
     * Used for the standalone `momentum` mod when no conversion is needed.
     */
    export struct [[nodiscard]] no_scroll_policy {
        static constexpr auto track_type   = EV_REL;
        static constexpr auto track_code_x = REL_WHEEL_HI_RES;
        static constexpr auto track_code_y = REL_HWHEEL_HI_RES;
        static constexpr auto emit_type    = EV_REL;
        static constexpr auto emit_code_x  = REL_WHEEL_HI_RES;
        static constexpr auto emit_code_y  = REL_HWHEEL_HI_RES;

        template <Context CtxT, typename MomentumT>
        static context_action pass_through(CtxT& ctx, MomentumT& momentum) noexcept {
            momentum.track(ctx.event());
            return context_action::next;
        }
    };

    /**
     * Scroll momentum policy: tracks raw mouse movement (REL_X/REL_Y),
     * converts to high-resolution scroll events (REL_WHEEL_HI_RES/REL_HWHEEL_HI_RES),
     * and emits scroll events during the momentum animation.
     *
     * Requires `mice_quantifier` earlier in the pipeline for discrete scroll steps.
     */
    export struct [[nodiscard]] scroll_momentum_policy {
        static constexpr auto track_type   = EV_REL;
        static constexpr auto track_code_x = REL_X;
        static constexpr auto track_code_y = REL_Y;
        static constexpr auto emit_type    = EV_REL;
        static constexpr auto emit_code_x  = REL_WHEEL_HI_RES;
        static constexpr auto emit_code_y  = REL_HWHEEL_HI_RES;

        /// Scroll sensitivity multiplier (positive = normal, negative = reversed).
        static constexpr event_type::value_type default_reverse = 8;

        template <Context CtxT, typename MomentumT>
        static context_action pass_through(CtxT& ctx, MomentumT& momentum) noexcept {
            using enum context_action;

            momentum.track(ctx.event());

            auto const& event = ctx.event();
            if (!is_mouse_movement(event)) {
                return next;
            }

            auto const hval = event.value() * default_reverse;
            switch (event.code()) {
                case REL_X: std::ignore = ctx.fork_emit(EV_REL, REL_HWHEEL_HI_RES, hval); break;
                case REL_Y: std::ignore = ctx.fork_emit(EV_REL, REL_WHEEL_HI_RES, hval); break;
                default: break;
            }

            if constexpr (has_mod<basic_mice_quantifier, CtxT>) {
                auto&      quant = ctx.mod(mice_quantifier);
                auto const val   = event.value();
                auto const cval  = (val > 0 ? 1 : val < 0 ? -1 : 0) * (default_reverse > 0 ? 1 : -1);
                if (auto const x_steps = quant.consume_x(); x_steps != 0) {
                    std::ignore = ctx.fork_emit(EV_REL, REL_HWHEEL, cval);
                }
                if (auto const y_steps = quant.consume_y(); y_steps != 0) {
                    std::ignore = ctx.fork_emit(EV_REL, REL_WHEEL, cval);
                }
            }

            return ignore_event;
        }
    };

    // ── Momentum engine ────────────────────────────────────────────────────

    /**
     * Non-template base class that owns the velocity trackers, animation
     * calculators, and all non-template-dependent logic.  Defined out-of-line
     * in the .cxx so the pimpl impl stays hidden.
     *
     * `basic_momentum<Policy>` inherits from this, adding only the
     * policy-provided constants and the template `operator()`/`run()`.
     */
    export struct [[nodiscard]] basic_momentum_base : pimpl_idiom<basic_momentum_base> {
        using pimpl_idiom::pimpl_idiom;
        using value_type = event_type::value_type;

        void track(event_type const& event) noexcept;

        void start(float pos_x, float delta_x, float vel_x, float pos_y, float delta_y, float vel_y) noexcept;

        [[nodiscard]] bool                    is_active() const noexcept;
        [[nodiscard]] std::pair<float, float> current_position() const noexcept;

        context_action operator()(start_tag) noexcept;

      protected:
        /// Configure which event codes to track and emit.
        /// Called by derived classes in their start_tag handler.
        void configure(event_type::type_type track_type, event_type::code_type track_code_x, event_type::code_type track_code_y) noexcept;

        [[nodiscard]] std::pair<float, float> position_at(fsecs elapsed) const noexcept;
        [[nodiscard]] fsecs                   calc_duration() const noexcept;
    };

    /**
     * Generic momentum pipeline mod, parameterized on a Policy.
     *
     * Tracks the velocity of axis events and can be triggered to generate
     * momentum-based synthetic events that gradually decelerate. The policy
     * defines what to track, what to emit, and how to transform events.
     *
     * @par Example
     * @code
     *   // Scroll momentum:
     *   auto pipeline = context
     *       | io_manager
     *       | input_manager
     *       | intercept[keyboard, mouse]
     *       | mice_quantifier
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

        /// Run the momentum animation synchronously, emitting events via
        /// `fork_emit` at ~60 fps.
        template <Context CtxT>
        void run(CtxT& ctx) noexcept {
            using enum context_action;
            auto const start_tp = std::chrono::steady_clock::now();
            auto const anim_dur = calc_duration();
            fsecs      elapsed{0};
            float      prev_x = 0.0f;
            float      prev_y = 0.0f;

            while (elapsed < anim_dur) {
                auto const now      = std::chrono::steady_clock::now();
                elapsed             = std::chrono::duration_cast<fsecs>(now - start_tp);
                auto const [px, py] = position_at(elapsed);
                auto const dx       = std::round(px - prev_x);
                auto const dy       = std::round(py - prev_y);
                prev_x              = px;
                prev_y              = py;

                if (dx != 0.0f || dy != 0.0f) {
                    if (dx != 0.0f) {
                        std::ignore = ctx.fork_emit(emit_type, emit_code_x, static_cast<value_type>(dx));
                    }
                    if (dy != 0.0f) {
                        std::ignore = ctx.fork_emit(emit_type, emit_code_y, static_cast<value_type>(dy));
                    }
                    std::ignore = ctx.fork_emit(EV_SYN, SYN_REPORT, 0);
                }

                std::this_thread::sleep_until(start_tp + std::chrono::duration_cast<usecs>(elapsed + frame_dt));
            }
        }

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

      private:
        static constexpr value_type fps = 60.0f;
        static constexpr fsecs      frame_dt{1.0 / static_cast<double>(fps)};
    };

    // ── Convenience aliases ────────────────────────────────────────────────

    /// Scroll momentum: tracks REL_X/REL_Y, emits REL_WHEEL_HI_RES during animation.
    export using momentum_scroll = basic_momentum<scroll_momentum_policy>;

    /// Standalone momentum engine (no event conversion, tracks scroll codes directly).
    export using momentum = basic_momentum<no_scroll_policy>;

} // namespace fs8
