// Created by moisrex on 6/9/25.

module;
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
        value_type                 reverse = -1;
        std::span<code_type const> hold_keys{default_scroll_keys}; // buttons to be hold

      public:
        constexpr basic_add_scroll() noexcept = default;

        constexpr explicit basic_add_scroll(std::span<code_type const> const inp_hold_keys,
                                            bool const                       inp_reverse = true) noexcept
          : reverse{inp_reverse ? 1 : -1},
            hold_keys{inp_hold_keys} {}

        consteval basic_add_scroll(basic_add_scroll const &) noexcept            = default;
        constexpr basic_add_scroll(basic_add_scroll &&) noexcept                 = default;
        consteval basic_add_scroll &operator=(basic_add_scroll const &) noexcept = default;
        constexpr basic_add_scroll &operator=(basic_add_scroll &&) noexcept      = default;
        constexpr ~basic_add_scroll() noexcept                                   = default;

        [[nodiscard]] consteval auto operator()(std::span<code_type const> const inp_hold_keys,
                                                bool const inp_reverse = true) const noexcept {
            return basic_add_scroll{inp_hold_keys, inp_reverse};
        }

        template <Context CtxT>
        [[nodiscard]] constexpr context_action operator()(CtxT &ctx) noexcept {
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
                if (!is_mouse_movement(event)) {
                    return ignore_event;
                }

                // release the held keys:
                for (auto const code : hold_keys) {
                    out.emit(EV_KEY, code, 0);
                }

                auto const val  = event.value();
                auto const cval = (val > 0 ? 1 : val < 0 ? -1 : 0) * reverse;
                if (auto const x_steps = quant.consume_x(); x_steps > 0) {
                    out.emit(EV_REL, REL_HWHEEL, cval);
                    // out.emit(event, EV_REL, REL_HWHEEL_HI_RES, cval * 120);
                    out.emit_syn();
                }
                if (auto const y_steps = quant.consume_y(); y_steps > 0) {
                    out.emit(EV_REL, REL_WHEEL, cval);
                    out.emit_syn();
                }

                if (event.code() == REL_X && val > 0) {
                    out.emit(EV_REL, REL_HWHEEL_HI_RES, val * 8);
                    out.emit_syn();
                }
                if (event.code() == REL_Y && val > 0) {
                    out.emit(EV_REL, REL_WHEEL_HI_RES, val * 8);
                    out.emit_syn();
                }

                lock = true;
                return ignore_event;
            }

            if (lock) {
                return ignore_event;
            }
            return next;
        }
    } add_scroll;
} // namespace foresight::mods
