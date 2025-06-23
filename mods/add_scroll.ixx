// Created by moisrex on 6/9/25.

module;
#include <cassert>
#include <linux/uinput.h>
#include <span>
export module foresight.mods.add_scroll;
import foresight.mods.context;
import foresight.mods.keys_status;
import foresight.mods.quantifier;
import foresight.mods.inout;
import foresight.mods.event;

export namespace foresight::mods {

    template <typename... T>
        requires((std::convertible_to<T, event_type::code_type> && ...))
    consteval std::array<event_type::code_type, sizeof...(T)> key_pack(T... vals) noexcept {
        return std::array<event_type::code_type, sizeof...(T)>{static_cast<event_type::code_type>(vals)...};
    }

    constexpr std::array<event_type::code_type, 1> default_scroll_keys{BTN_MIDDLE};

    constexpr struct [[nodiscard]] basic_add_scroll {
        using value_type = event_type::value_type;
        using code_type  = event_type::code_type;

      private:
        bool                       lock    = false;
        value_type                 reverse = 8;
        std::span<code_type const> hold_keys{default_scroll_keys}; // buttons to be hold

      public:
        constexpr basic_add_scroll() noexcept = default;

        constexpr explicit basic_add_scroll(std::span<code_type const> const inp_hold_keys,
                                            value_type const                 inp_reverse = 8) noexcept
          : reverse{inp_reverse},
            hold_keys{inp_hold_keys} {
            assert(reverse != 0);
        }

        consteval basic_add_scroll(basic_add_scroll const &) noexcept            = default;
        constexpr basic_add_scroll(basic_add_scroll &&) noexcept                 = default;
        consteval basic_add_scroll &operator=(basic_add_scroll const &) noexcept = default;
        constexpr basic_add_scroll &operator=(basic_add_scroll &&) noexcept      = default;
        constexpr ~basic_add_scroll() noexcept                                   = default;

        template <typename... Args>
        [[nodiscard]] consteval auto operator()(Args &&...args) const noexcept {
            return basic_add_scroll{std::forward<Args>(args)...};
        }

        template <Context CtxT>
        [[nodiscard]] context_action operator()(CtxT &ctx) noexcept {
            using enum context_action;
            static_assert(has_mod<basic_keys_status, CtxT>, "We need access to keys' statuses.");
            static_assert(has_mod<basic_mice_quantifier, CtxT>, "We need access quantifier.");

            // we don't need keys and quantifier mods taken like this, but it's done for readability
            auto &keys  = ctx.mod(keys_status);
            auto &quant = ctx.mod(mice_quantifier);
            auto &out   = ctx.mod(output_mod);
            auto &event = ctx.event();

            // Hold the lock, until both buttons are released.
            // This helps to stop accidental clicks while scrolling.
            if (keys.is_released(hold_keys)) {
                lock = false;
            }


            if (keys.is_pressed(hold_keys)) {
                // release the held keys:
                for (auto const code : hold_keys) {
                    std::ignore = ctx.fork_emit(EV_KEY, code, 0);
                    std::ignore = ctx.fork_emit(syn());
                }

                if (is_mouse_movement(event)) {
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

                    lock = true;
                    return ignore_event;
                }
            }

            if (lock && is_mouse_event(event)) {
                return ignore_event;
            }
            return next;
        }
    } add_scroll;
} // namespace foresight::mods
