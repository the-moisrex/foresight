// Created by moisrex on 6/9/25.

module;
#include <cassert>
#include <cmath>
#include <linux/uinput.h>
export module foresight.mods.quantifier;
import foresight.mods.event;
import foresight.mods.context;

export namespace foresight {


    /**
     * If you need to act upon each N pixels movement, then this is what you need.
     * This works for SINGLE event code.
     */
    constexpr struct [[nodiscard]] basic_quantifier {
        using value_type = event_type::value_type;
        using code_type  = event_type::code_type;

      private:
        value_type step     = 10; // pixels/units
        value_type value    = 0;

      public:
        constexpr basic_quantifier() noexcept = default;

        constexpr explicit basic_quantifier(value_type const inp_step) noexcept : step{inp_step} {
            assert(step > 0);
        }

        consteval basic_quantifier(basic_quantifier const&)                = default;
        constexpr basic_quantifier(basic_quantifier&&) noexcept            = default;
        consteval basic_quantifier& operator=(basic_quantifier const&)     = default;
        constexpr basic_quantifier& operator=(basic_quantifier&&) noexcept = default;
        constexpr ~basic_quantifier() noexcept                             = default;

        constexpr void process(event_type const& event, code_type const btn_code) noexcept {
            if (event.type() != EV_REL || event.code() != btn_code) {
                return;
            }
            value += event.value();
        }

        [[nodiscard]] constexpr value_type consume_steps() noexcept {
            auto const step_count  = std::abs(value) / step;
            value                 %= step;
            return step_count;
        }
    } quantifier;

    /**
     * If you want to quantify your mouse movements into steps, this is what you need.
     */
    constexpr struct [[nodiscard]] basic_mice_quantifier {
        using value_type = event_type::value_type;
        using code_type  = event_type::code_type;

      private:
        value_type step       = 10; // pixels/units
        value_type x_value    = 0;
        value_type y_value    = 0;

      public:
        constexpr basic_mice_quantifier() noexcept = default;

        constexpr explicit basic_mice_quantifier(value_type const inp_step) noexcept : step{inp_step} {
            assert(step > 0);
        }

        consteval basic_mice_quantifier(basic_mice_quantifier const&)                = default;
        constexpr basic_mice_quantifier(basic_mice_quantifier&&) noexcept            = default;
        consteval basic_mice_quantifier& operator=(basic_mice_quantifier const&)     = default;
        constexpr basic_mice_quantifier& operator=(basic_mice_quantifier&&) noexcept = default;
        constexpr ~basic_mice_quantifier() noexcept                                  = default;

        constexpr void process(event_type const& event) noexcept {
            if (event.type() != EV_REL) {
                return;
            }
            switch (event.code()) {
                case REL_X: x_value += event.value(); break;
                case REL_Y: y_value += event.value(); break;
                default: break;
            }
        }

        [[nodiscard]] constexpr value_type consume_x() noexcept {
            auto const step_count  = std::abs(x_value) / step;
            x_value               %= step;
            return step_count;
        }

        [[nodiscard]] constexpr value_type consume_y() noexcept {
            auto const step_count  = std::abs(y_value) / step;
            y_value               %= step;
            return step_count;
        }

        // This can be used like:
        //    mise_quantifier(20)
        consteval basic_mice_quantifier operator()(value_type const steps) const noexcept {
            return basic_mice_quantifier{steps};
        }

        constexpr void operator()(Context auto& ctx) noexcept {
            process(ctx.event());
        }
    } mice_quantifier;


} // namespace foresight
