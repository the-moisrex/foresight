// Created by moisrex on 6/18/25.

module;
#include <linux/input-event-codes.h>
#include <span>
export module foresight.mods.on;

import foresight.mods.context;

namespace foresight::mods {

    using ev_type    = event_type::type_type;
    using code_type  = event_type::code_type;
    using value_type = event_type::value_type;

    export constexpr struct basic_noop {
        constexpr void operator()() const noexcept {
            // do nothing
        }
    } noop;

    export struct [[nodiscard]] pressed {
        ev_type    type  = EV_MAX;
        code_type  code  = KEY_MAX;
        value_type value = 1;
    };

    export struct [[nodiscard]] released {
        ev_type    type  = EV_MAX;
        code_type  code  = KEY_MAX;
        value_type value = 0;
    };

    export template <typename Func = basic_noop>
    struct [[nodiscard]] basic_on {
      private:
        ev_type                    type{};
        code_type                  code{};
        value_type                 value{};
        [[no_unique_address]] Func func; // action to be called

        template <typename>
        friend struct basic_on;

      public:
        template <typename InpFunc>
            requires(std::convertible_to<InpFunc, Func>)
        constexpr explicit basic_on(InpFunc&& inp_func) noexcept : func{std::forward<InpFunc>(inp_func)} {}

        constexpr basic_on() noexcept(std::is_nothrow_constructible_v<Func>) = default;
        constexpr basic_on(basic_on&&) noexcept                              = default;
        consteval basic_on(basic_on const&) noexcept                         = default;
        constexpr basic_on& operator=(basic_on&&) noexcept                   = default;
        consteval basic_on& operator=(basic_on const&) noexcept              = default;
        constexpr ~basic_on()                                                = default;

        template <typename InpFunc>
        consteval auto operator()(InpFunc&& inp_func) const noexcept {
            return basic_on<std::remove_cvref_t<InpFunc>>(std::forward<InpFunc>(inp_func));
        }

        template <typename EvTempl, typename InpFunc>
        consteval auto operator()(EvTempl templ, InpFunc inp_func) const noexcept {
            auto res  = basic_on<std::remove_cvref_t<InpFunc>>{inp_func};
            res.type  = templ.type;
            res.code  = templ.code;
            res.value = templ.value;
            return res;
        }

        template <Context CtxT>
        constexpr context_action operator()(CtxT& ctx) noexcept(std::is_nothrow_invocable_v<Func, CtxT&>) {
            auto const& event = ctx.event();
            if ((event.type() == type || type == EV_MAX) && (event.code() == code || code == KEY_MAX) &&
                event.value() == value)
            {
                if constexpr (std::invocable<Func, CtxT&>) {
                    return invoke_mod(func, ctx);
                } else {
                    func();
                }
            }
            return context_action::next;
        }
    };

    export constexpr basic_on<> on;


} // namespace foresight::mods
