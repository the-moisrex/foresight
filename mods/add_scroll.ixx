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
import foresight.main.utils;

export namespace foresight {

    template <typename... T>
        requires((std::convertible_to<T, event_type::code_type> && ...))
    consteval std::array<event_type::code_type, sizeof...(T)> key_pack(T... vals) noexcept {
        return std::array<event_type::code_type, sizeof...(T)>{static_cast<event_type::code_type>(vals)...};
    }

    template <typename CondT = basic_noop, typename Func = basic_noop>
    struct [[nodiscard]] basic_add_scroll {
        using value_type = event_type::value_type;
        using code_type  = event_type::code_type;

      private:
        bool                        lock    = false;
        value_type                  reverse = 5;
        [[no_unique_address]] CondT cond;
        [[no_unique_address]] Func  func;

      public:
        constexpr basic_add_scroll() noexcept = default;

        constexpr explicit basic_add_scroll(
          CondT const     &inp_cond,
          value_type const inp_reverse = 5,
          Func const      &inp_func    = {}) noexcept
          : reverse{inp_reverse},
            cond{inp_cond},
            func{inp_func} {
            assert(reverse != 0);
        }

        consteval basic_add_scroll(basic_add_scroll const &) noexcept            = default;
        constexpr basic_add_scroll(basic_add_scroll &&) noexcept                 = default;
        consteval basic_add_scroll &operator=(basic_add_scroll const &) noexcept = default;
        constexpr basic_add_scroll &operator=(basic_add_scroll &&) noexcept      = default;
        constexpr ~basic_add_scroll() noexcept                                   = default;

        [[nodiscard]] constexpr bool is_locked() const noexcept {
            return lock;
        }

        void operator()(auto &&, tag auto) = delete;

        template <typename InpCondT, typename InpFuncT = basic_noop>
        [[nodiscard]] consteval auto operator()(
          InpCondT const  &inp_cond,
          value_type const inp_reverse = 8,
          InpFuncT const  &inp_func    = {}) const noexcept {
            return basic_add_scroll<InpCondT, InpFuncT>{inp_cond, inp_reverse, inp_func};
        }

        template <typename InpCondT, typename InpFuncT = basic_noop>
        [[nodiscard]] consteval auto operator()(InpCondT const &inp_cond,
                                                InpFuncT const &inp_func = {}) const noexcept {
            return basic_add_scroll<InpCondT, InpFuncT>{inp_cond, reverse, inp_func};
        }

        template <Context CtxT>
        [[nodiscard]] context_action operator()(CtxT &ctx) noexcept {
            using enum context_action;
            static_assert(has_mod<basic_keys_status, CtxT>, "We need access to keys' statuses.");
            static_assert(has_mod<basic_mice_quantifier, CtxT>, "We need access quantifier.");

            // we don't need keys and quantifier mods taken like this, but it's done for readability
            auto const &keys  = ctx.mod(keys_status);
            auto       &quant = ctx.mod(mice_quantifier);
            auto const &event = ctx.event();


            if (invoke_cond(cond, ctx)) {
                // release the held keys:
                if (!lock) {
                    std::ignore = invoke_mod(func, ctx);
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
            } else {
                lock = false;
                return next;
            }

            if (lock && is_mouse_movement(event)) {
                return ignore_event;
            }
            return next;
        }
    };

    constexpr basic_add_scroll<> add_scroll;

} // namespace foresight
