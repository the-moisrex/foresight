// Created by moisrex on 6/9/25.

module;
#include <cassert>
#include <linux/uinput.h>
#include <span>
export module fs8.mods:mouse_to_scroll;
import fs8.context;
import :quantifier;
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
    /// Requires `mice_quantifier` earlier in the pipeline for step accumulation.
    constexpr struct [[nodiscard]] basic_mouse_to_scroll : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;

      private:
        value_type reverse = 8;

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
            static_assert(has_mod<basic_mice_quantifier, CtxT>, "We need access quantifier.");

            auto&       quant = ctx.mod(mice_quantifier);
            auto const& event = ctx.event();

            if (!is_mouse_movement(event)) {
                return next;
            }

            auto const val  = event.value();
            auto const code = event.code();
            auto const cval = (val > 0 ? 1 : val < 0 ? -1 : 0) * (reverse > 0 ? 1 : -1);
            if (auto const x_steps = quant.consume_x(); x_steps != 0) {
                std::ignore = ctx.fork_emit(EV_REL, REL_HWHEEL, cval);
            }
            if (auto const y_steps = quant.consume_y(); y_steps != 0) {
                std::ignore = ctx.fork_emit(EV_REL, REL_WHEEL, cval);
            }

            auto const hval = val * reverse;
            switch (code) {
                case REL_X: std::ignore = ctx.fork_emit(EV_REL, REL_HWHEEL_HI_RES, hval); break;
                case REL_Y: std::ignore = ctx.fork_emit(EV_REL, REL_WHEEL_HI_RES, hval); break;
                default: break;
            }
            return ignore_event;
        }
    } mouse_to_scroll;

} // namespace fs8
