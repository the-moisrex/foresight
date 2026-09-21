// Created by moisrex on 9/17/26.

module;
#include <chrono>
#include <cmath>
#include <linux/input-event-codes.h>
#include <type_traits>
export module fs8.mods:velocity;
import fs8.context;
import fs8.event;
import fs8.traits;

export namespace fs8 {

    // ── Event source tags ────────────────────────────────────────────────────

    /// Track REL_X/REL_Y deltas (mouse movement).
    struct [[nodiscard]] rel {
        static constexpr float default_threshold = 500.0f;
        static constexpr float ema_tau           = 0.1f;

      private:
        float x_accum = 0.0f;
        float y_accum = 0.0f;

      public:
        void accumulate(event_type const& event) noexcept {
            if (event.type() == EV_REL) {
                if (event.code() == REL_X) {
                    x_accum += static_cast<float>(event.value());
                } else if (event.code() == REL_Y) {
                    y_accum += static_cast<float>(event.value());
                }
            }
        }

        [[nodiscard]] float total_delta() const noexcept {
            return std::sqrt((x_accum * x_accum) + (y_accum * y_accum));
        }

        void reset_frame() noexcept {
            x_accum = 0.0f;
            y_accum = 0.0f;
        }
    };

    /// Track ABS_X/ABS_Y position deltas (tablet pen movement).
    struct [[nodiscard]] abs_position {
        using value_type = event_type::value_type;

        static constexpr float default_threshold = 500.0f;
        static constexpr float ema_tau           = 0.1f;

      private:
        float      x_accum    = 0.0f;
        float      y_accum    = 0.0f;
        value_type prev_abs_x = 0;
        value_type prev_abs_y = 0;
        bool       has_prev   = false;

      public:
        void accumulate(event_type const& event) noexcept {
            if (event.type() == EV_ABS) {
                if (event.code() == ABS_X) {
                    auto const val = event.value();
                    if (has_prev) {
                        x_accum += static_cast<float>(val - prev_abs_x);
                    }
                    prev_abs_x = val;
                    has_prev   = true;
                } else if (event.code() == ABS_Y) {
                    auto const val = event.value();
                    if (has_prev) {
                        y_accum += static_cast<float>(val - prev_abs_y);
                    }
                    prev_abs_y = val;
                    has_prev   = true;
                }
            }
        }

        [[nodiscard]] float total_delta() const noexcept {
            return std::sqrt((x_accum * x_accum) + (y_accum * y_accum));
        }

        void reset_frame() noexcept {
            x_accum  = 0.0f;
            y_accum  = 0.0f;
            has_prev = false;
        }
    };

    /// Track ABS_Z/ABS_PRESSURE deltas (pen pressure changes).
    struct [[nodiscard]] abs_pressure {
        static constexpr float default_threshold = 1.0f;
        static constexpr float ema_tau           = 0.1f;

      private:
        float x_accum = 0.0f;

      public:
        void accumulate(event_type const& event) noexcept {
            if (event.type() == EV_ABS && (event.code() == ABS_PRESSURE || event.code() == ABS_Z)) {
                x_accum += static_cast<float>(event.value());
            }
        }

        [[nodiscard]] float total_delta() const noexcept {
            return std::abs(x_accum);
        }

        void reset_frame() noexcept {
            x_accum = 0.0f;
        }
    };

    // ── Velocity mod ─────────────────────────────────────────────────────────

    /**
     * Gate an inner mod based on movement velocity.
     *
     * Accumulates movement events, computes velocity on each SYN_REPORT,
     * and either delegates to the inner mod (fast) or passes events through
     * unchanged (slow). This fixes cursor acceleration amplification: slow
     * movements stay as a single frame, fast movements get decomposed.
     *
     * Three event sources are supported via tag types:
     * - `rel` (default): REL_X/REL_Y — mouse movement velocity.
     * - `abs_position`: ABS_X/ABS_Y — tablet pen movement velocity.
     * - `abs_pressure`: ABS_Z/ABS_PRESSURE — pen pressure change velocity.
     *
     * @par Examples
     * @code
     *   | velocity[split_move]                          // REL, default threshold
     *   | velocity[800.0f, split_move]                  // REL, custom threshold
     *   | velocity[abs_position, split_move]            // ABS position
     *   | velocity[abs_position, 800.0f, split_move]    // ABS position, custom
     *   | velocity[abs_pressure, split_move]            // ABS pressure
     *   | velocity[abs_pressure, 2.0f, split_move]      // ABS pressure, custom
     * @endcode
     */
    template <typename EventSource = rel, typename InnerMod = void>
    struct [[nodiscard]] basic_velocity : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using time_type  = event_type::time_type;
        using msecs      = std::chrono::microseconds;
        using inner_type = std::conditional_t<std::is_void_v<InnerMod>, int, InnerMod>;

        static constexpr float usec_to_sec = 1.0e-6f;

      private:
        float                             threshold = EventSource::default_threshold;
        [[no_unique_address]] EventSource source{};
        [[no_unique_address]] inner_type  inner{};

        float smoothed_velocity = 0.0f;
        msecs last_timestamp{};
        bool  has_velocity = false;
        bool  first_frame  = true;

        /// True when the velocity from the previous frame was above threshold.
        /// The current frame's events are delegated to the inner mod when this is set.
        bool velocity_above_threshold = false;

        void process_velocity(float const delta, msecs const timestamp) noexcept {
            if (last_timestamp.count() == 0) {
                last_timestamp = timestamp;
                return;
            }
            auto const  dt_duration = timestamp - last_timestamp;
            auto const  dt_us       = dt_duration.count();
            float const dt          = static_cast<float>(dt_us) * usec_to_sec;
            if (dt < usec_to_sec) {
                return;
            }
            float const     v_instant = delta / dt;
            constexpr float tau       = EventSource::ema_tau;
            float const     alpha     = 1.0f - std::exp(-dt / tau);
            smoothed_velocity         = (alpha * v_instant) + ((1.0f - alpha) * smoothed_velocity);
            last_timestamp            = timestamp;
            has_velocity              = true;
        }

        template <typename OtherSource, typename OtherInner>
        friend struct basic_velocity;

      public:
        constexpr basic_velocity() noexcept = default;

        explicit constexpr basic_velocity(float const inp_threshold) noexcept : threshold{inp_threshold} {}

        template <typename Inner>
            requires(std::is_nothrow_copy_constructible_v<Inner>
                     && (!std::is_same_v<Inner, void>)
                     && (!std::same_as<std::remove_cvref_t<Inner>, get_variables_tag>) )
        consteval auto operator[](Inner inp_inner) const noexcept {
            basic_velocity<EventSource, Inner> result{};
            result.threshold = threshold;
            result.inner     = inp_inner;
            return result;
        }

        template <typename Inner>
            requires(std::is_nothrow_copy_constructible_v<Inner>
                     && (!std::is_same_v<Inner, void>)
                     && (!std::same_as<std::remove_cvref_t<Inner>, get_variables_tag>) )
        consteval auto operator[](float const inp_threshold, Inner inp_inner) const noexcept {
            basic_velocity<EventSource, Inner> result{};
            result.threshold = inp_threshold;
            result.inner     = inp_inner;
            return result;
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;
            auto& event = ctx.event();

            source.accumulate(event);

            // Non-SYN events: delegate to inner if velocity is above threshold, otherwise pass through.
            if (event.type() != EV_SYN || event.code() != SYN_REPORT) {
                if constexpr (!std::is_void_v<InnerMod>) {
                    if (velocity_above_threshold) {
                        return invoke_mod(inner, ctx);
                    }
                }
                return next;
            }

            // SYN: compute velocity from accumulated frame, update threshold decision for next frame.
            auto const timestamp   = event.micro_time();
            auto const total_delta = source.total_delta();

            if (first_frame) {
                first_frame = false;
                process_velocity(total_delta, timestamp);
                source.reset_frame();
                return next;
            }

            process_velocity(total_delta, timestamp);
            source.reset_frame();

            velocity_above_threshold = has_velocity && (smoothed_velocity >= threshold);

            if constexpr (!std::is_void_v<InnerMod>) {
                if (velocity_above_threshold) {
                    return invoke_mod(inner, ctx);
                }
            }
            return next;
        }

        context_action operator()(special_event const& tag) noexcept {
            if (tag.code == start.code) {
                smoothed_velocity        = 0.0f;
                last_timestamp           = msecs::zero();
                has_velocity             = false;
                first_frame              = true;
                velocity_above_threshold = false;
                source.reset_frame();
                return context_action::next;
            }
            return context_action::drop_event;
        }

        [[nodiscard]] float get_velocity() const noexcept {
            return smoothed_velocity;
        }

        [[nodiscard]] float get_threshold() const noexcept {
            return threshold;
        }
    };

    // ── Convenience aliases ──────────────────────────────────────────────────

    /// REL events velocity gating.
    using velocity_rel = basic_velocity<rel>;

    /// ABS position velocity gating.
    using velocity_abs_position = basic_velocity<abs_position>;

    /// ABS pressure velocity gating.
    using velocity_abs_pressure = basic_velocity<abs_pressure>;

    /// Default velocity instance (REL events).
    constexpr velocity_rel velocity;

    // ── Concept checks ───────────────────────────────────────────────────────

    static_assert(Modifier<velocity_rel>);
    static_assert(Modifier<velocity_abs_position>);
    static_assert(Modifier<velocity_abs_pressure>);

} // namespace fs8
