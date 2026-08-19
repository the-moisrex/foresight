module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <linux/input-event-codes.h>
export module fs8.mods.smooth;
import fs8.mods.mouse_status;
import fs8.event;
import fs8.context;
import fs8.easings;
import fs8.pimpl;

export namespace fs8 {

    /**
     * Linear Interpolation.
     *
     * Breaks a frame's mouse movement `(dx, dy)` into several smaller steps so
     * the cursor glides instead of jumping in one event. The total emitted
     * movement equals the input frame exactly (drift-free), and every step
     * emits `REL_X`, `REL_Y` and `SYN` together.
     *
     * The number of steps is derived from the frame's magnitude
     * (`max(|dx|, |dy|)`) and capped at `max_steps`. The `easing` function
     * shapes the curve (default `easeOutQuad`).
     *
     * @par Example
     * @code
     *   ... | mouse_history | lerp | output
     *   ... | mouse_history | lerp[8] | output
     *   ... | mouse_history | lerp[8, fs8::easeInOutQuad<float>] | output
     * @endcode
     *
     * Requires `mouse_history` placed before this mod in the pipeline.
     */
    constexpr struct [[nodiscard]] basic_lerp : pimpl_idiom<basic_lerp> {
        using pimpl_idiom::pimpl_idiom;

        using value_type = event_type::value_type;

      private:
        std::size_t max_steps  = 16;
        float (*easing)(float) = easeOutQuad<float>;

        void mark_movement() noexcept;
        bool take_frame() noexcept;

      public:
        constexpr explicit basic_lerp(std::size_t const inp_max_steps, float (*const inp_easing)(float)) noexcept
          : max_steps{std::max<std::size_t>(1, inp_max_steps)},
            easing{inp_easing} {}

        consteval basic_lerp operator[](std::size_t const inp_max_steps) const noexcept {
            return basic_lerp{inp_max_steps, easeOutQuad<float>};
        }

        consteval basic_lerp operator[](std::size_t const inp_max_steps, float (*const inp_easing)(float)) const noexcept {
            return basic_lerp{inp_max_steps, inp_easing};
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;
            static_assert(has_mod<basic_mouse_history<>, CtxT>, "smooth needs mouse_history in the pipeline, placed before it.");

            auto& event = ctx.event();
            if (is_mouse_movement(event)) {
                mark_movement();
                return ignore_event;
            }
            if (!is_syn(event) || !take_frame()) {
                return next;
            }

            auto const cur = ctx.mod(mouse_history).cur();
            auto const mag = std::max(std::abs(cur.x), std::abs(cur.y));
            if (mag == 0) {
                return next;
            }
            std::int32_t const steps = std::min<std::int32_t>(mag, static_cast<std::int32_t>(max_steps));

            value_type prev_x = 0;
            value_type prev_y = 0;
            for (std::int32_t step = 1; step <= steps; ++step) {
                auto const t     = easing(static_cast<float>(step) / static_cast<float>(steps));
                auto const cur_x = static_cast<value_type>(std::round(t * static_cast<float>(cur.x)));
                auto const cur_y = static_cast<value_type>(std::round(t * static_cast<float>(cur.y)));
                auto const rel_x = cur_x - prev_x;
                auto const rel_y = cur_y - prev_y;
                prev_x           = cur_x;
                prev_y           = cur_y;
                if (rel_x == 0 && rel_y == 0) {
                    continue;
                }
                std::ignore = ctx.fork_emit(EV_REL, REL_X, rel_x);
                std::ignore = ctx.fork_emit(EV_REL, REL_Y, rel_y);
                std::ignore = ctx.fork_emit(syn());
            }

            return ignore_event;
        }
    } lerp;

    /**
     * Low-pass filter for mouse movement.
     *
     * Smooths each frame's movement `(dx, dy)` with an exponential moving
     * average:
     *
     *     smoothed = α × newInput + (1 − α) × previousSmoothed
     *
     * A smaller `α` smooths more (keeps more history), a larger `α` tracks the
     * raw input closer. `α` is clamped to `(0, 1]`. The first movement frame
     * passes through at full strength, and frames with no movement emit
     * nothing (no residual drift).
     *
     * @par Example
     * @code
     *   ... | mouse_history | low_pass_filter | output
     *   ... | mouse_history | low_pass_filter[0.4f] | output
     * @endcode
     *
     * Requires `mouse_history` placed before this mod in the pipeline.
     */
    constexpr struct [[nodiscard]] basic_low_pass_filter : pimpl_idiom<basic_low_pass_filter> {
        using pimpl_idiom::pimpl_idiom;

        using value_type = event_type::value_type;

        struct [[nodiscard]] smoothed {
            bool       emit = false;
            value_type x    = 0;
            value_type y    = 0;
        };

      private:
        float alpha = 0.9f;

        void     mark_movement() noexcept;
        smoothed filter_frame(value_type cur_x, value_type cur_y) noexcept;

      public:
        constexpr explicit basic_low_pass_filter(float const inp_alpha) noexcept : alpha{std::clamp(inp_alpha, 0.f, 1.f)} {}

        consteval basic_low_pass_filter operator[](float const inp_alpha) const noexcept {
            return basic_low_pass_filter{inp_alpha};
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;
            static_assert(has_mod<basic_mouse_history<>, CtxT>, "smooth needs mouse_history in the pipeline, placed before it.");

            auto& event = ctx.event();
            if (is_mouse_movement(event)) {
                mark_movement();
                return ignore_event;
            }
            if (!is_syn(event)) {
                return next;
            }

            auto const cur = ctx.mod(mouse_history).cur();
            auto const out = filter_frame(cur.x, cur.y);
            if (!out.emit) {
                return next;
            }

            std::ignore = ctx.fork_emit(EV_REL, REL_X, out.x);
            std::ignore = ctx.fork_emit(EV_REL, REL_Y, out.y);
            event.reset_time();
            return next;
        }
    } low_pass_filter;

    /**
     * Kalman filter for mouse movement.
     *
     * Tracks a smoothed estimate of each frame's movement `(dx, dy)` with a
     * one-dimensional Kalman filter per axis:
     *
     *     Predict: P = P + Q
     *     Gain:    K = P / (P + R)
     *     Update:  estimate = estimate + K × (measurement − estimate)
     *              P = P × (1 − K)
     *
     * `Q` is the process noise (how much the movement is expected to change),
     * `R` is the measurement noise (how noisy the input is). Both are clamped
     * to be positive. The first movement frame passes through at full strength
     * and frames with no movement emit nothing (no residual drift).
     *
     * @par Example
     * @code
     *   ... | mouse_history | kalman_filter | output
     *   ... | mouse_history | kalman_filter[0.05f, 0.8f] | output
     * @endcode
     *
     * Requires `mouse_history` placed before this mod in the pipeline.
     */
    constexpr struct [[nodiscard]] basic_kalman_filter : pimpl_idiom<basic_kalman_filter> {
        using pimpl_idiom::pimpl_idiom;

        using value_type = event_type::value_type;

        struct [[nodiscard]] smoothed {
            bool       emit = false;
            value_type x    = 0;
            value_type y    = 0;
        };

      private:
        float q = 0.1f;
        float r = 0.5f;

        void     mark_movement() noexcept;
        smoothed filter_frame(value_type cur_x, value_type cur_y) noexcept;

      public:
        constexpr explicit basic_kalman_filter(float const inp_q, float const inp_r = 0.5f) noexcept
          : q{std::max(inp_q, 1e-6f)},
            r{std::max(inp_r, 1e-6f)} {}

        consteval basic_kalman_filter operator[](float const inp_q, float const inp_r = 0.5f) const noexcept {
            return basic_kalman_filter{inp_q, inp_r};
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;
            static_assert(has_mod<basic_mouse_history<>, CtxT>, "smooth needs mouse_history in the pipeline, placed before it.");

            auto& event = ctx.event();
            if (is_mouse_movement(event)) {
                mark_movement();
                return ignore_event;
            }
            if (!is_syn(event)) {
                return next;
            }

            auto const cur = ctx.mod(mouse_history).cur();
            auto const out = filter_frame(cur.x, cur.y);
            if (!out.emit) {
                return next;
            }

            std::ignore = ctx.fork_emit(EV_REL, REL_X, out.x);
            std::ignore = ctx.fork_emit(EV_REL, REL_Y, out.y);
            event.reset_time();
            return next;
        }
    } kalman_filter;

} // namespace fs8
