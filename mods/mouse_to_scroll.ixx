// Created by moisrex on 6/9/25.

module;
#include <cassert>
#include <cmath>
#include <linux/uinput.h>
#include <span>
export module fs8.mods:mouse_to_scroll;
import fs8.context;
import fs8.event;
import fs8.traits;

export namespace fs8 {

    /// Convert mouse movement into scroll-wheel events.
    ///
    /// A pure transformer: it has no condition of its own. While it's active it
    /// swallows mouse movement (`REL_X`/`REL_Y`) and re-emits it as scroll
    /// events (`REL_WHEEL`/`REL_HWHEEL` + high-res variants). Gate it with
    /// `hold_mod` (e.g. `hold_mod[KEY_CAPSLOCK, BTN_MIDDLE, mouse_to_scroll]`)
    /// so it only runs while a scroll modifier key is held.
    ///
    /// Accumulates mouse movement internally and quantizes it into scroll
    /// notches: legacy events (±1 per notch) are emitted when the accumulated
    /// value crosses the step threshold, while hi-res events carry the full
    /// non-quantized value.
    constexpr struct [[nodiscard]] basic_mouse_to_scroll : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;

      private:
        value_type reverse = 8;
        value_type step    = 10; // pixels per scroll notch

        /// Accumulated (signed) mouse deltas awaiting quantization.
        value_type x_accum = 0;
        value_type y_accum = 0;

      public:
        constexpr explicit basic_mouse_to_scroll(value_type const inp_reverse) noexcept : reverse{inp_reverse} {
            assert(reverse != 0);
        }

        consteval basic_mouse_to_scroll operator[](value_type const inp_reverse) const noexcept {
            return basic_mouse_to_scroll{inp_reverse};
        }

        template <Context CtxT>
        [[nodiscard]] context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;

            auto const& event = ctx.event();

            if (!is_mouse_movement(event)) {
                return next;
            }

            // Accumulate movement for step quantization.
            switch (event.code()) {
                case REL_X: x_accum += event.value(); break;
                case REL_Y: y_accum += event.value(); break;
                default: break;
            }

            // Emit legacy scroll events (±1 per notch) when the accumulated
            // value crosses the step threshold.
            auto sign_x = [](value_type v) noexcept -> value_type {
                return (v > 0) - (v < 0);
            };
            auto const sign_reverse = reverse > 0 ? 1 : -1;

            while (x_accum >= step) {
                x_accum     -= step;
                std::ignore  = ctx.fork_emit(EV_REL, REL_HWHEEL, sign_x(1) * sign_reverse);
            }
            while (x_accum <= -step) {
                x_accum     += step;
                std::ignore  = ctx.fork_emit(EV_REL, REL_HWHEEL, sign_x(-1) * sign_reverse);
            }
            while (y_accum >= step) {
                y_accum     -= step;
                std::ignore  = ctx.fork_emit(EV_REL, REL_WHEEL, sign_x(1) * sign_reverse);
            }
            while (y_accum <= -step) {
                y_accum     += step;
                std::ignore  = ctx.fork_emit(EV_REL, REL_WHEEL, sign_x(-1) * sign_reverse);
            }

            // Hi-res (non-quantized): carry the full value.
            auto const hval = event.value() * reverse;
            switch (event.code()) {
                case REL_X: std::ignore = ctx.fork_emit(EV_REL, REL_HWHEEL_HI_RES, hval); break;
                case REL_Y: std::ignore = ctx.fork_emit(EV_REL, REL_WHEEL_HI_RES, hval); break;
                default: break;
            }
            return ignore_event;
        }
    } mouse_to_scroll;

} // namespace fs8
