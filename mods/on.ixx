// Created by moisrex on 6/18/25.

module;
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <linux/input-event-codes.h>
#include <print>
#include <tuple>
export module foresight.mods.on;

import foresight.mods.keys_status;
import foresight.mods.context;

namespace foresight {

    using ev_type    = event_type::type_type;
    using code_type  = event_type::code_type;
    using value_type = event_type::value_type;

    export constexpr struct basic_noop {
        constexpr void operator()() const noexcept {
            // do nothing
        }

        [[nodiscard]] constexpr bool operator()([[maybe_unused]] Context auto&) const noexcept {
            return false;
        }
    } noop;

    export template <typename CondFunc = basic_noop, typename Func = basic_noop>
    struct [[nodiscard]] basic_on {
      private:
        [[no_unique_address]] CondFunc cond;
        [[no_unique_address]] Func     func; // action to be called

        template <typename, typename>
        friend struct basic_on;

      public:
        template <typename InpCond, typename InpFunc>
            requires(std::convertible_to<InpCond, CondFunc> && std::convertible_to<InpFunc, Func>)
        constexpr explicit basic_on(InpCond&& inp_cond, InpFunc&& inp_func) noexcept
          : cond{std::forward<InpCond>(inp_cond)},
            func{std::forward<InpFunc>(inp_func)} {}

        constexpr basic_on() noexcept(std::is_nothrow_constructible_v<Func>) = default;
        constexpr basic_on(basic_on&&) noexcept                              = default;
        consteval basic_on(basic_on const&) noexcept                         = default;
        constexpr basic_on& operator=(basic_on&&) noexcept                   = default;
        consteval basic_on& operator=(basic_on const&) noexcept              = default;
        constexpr ~basic_on()                                                = default;

        template <typename EvTempl, typename InpFunc>
        consteval auto operator()(EvTempl templ, InpFunc inp_func) const noexcept {
            return basic_on<std::remove_cvref_t<EvTempl>, std::remove_cvref_t<InpFunc>>{templ, inp_func};
        }

        template <typename EvTempl, typename InpFunc, typename... Args>
            requires(sizeof...(Args) >= 1)
        consteval auto operator()(EvTempl templ, InpFunc inp_func, Args&&... args) const noexcept {
            auto const cmd = [inp_func, args...]() constexpr noexcept {
                static_assert(std::is_nothrow_invocable_v<InpFunc, Args...>, "Make it nothrow");
                std::invoke(inp_func, std::forward<Args>(args)...);
            };
            using cmd_type = std::remove_cvref_t<decltype(cmd)>;
            return basic_on<std::remove_cvref_t<EvTempl>, cmd_type>{templ, cmd};
        }

        template <Context CtxT>
        constexpr context_action operator()(CtxT& ctx) noexcept {
            if (cond(ctx)) {
                auto view = ctx.fork_view(*this);
                return invoke_mod(func, view);
            }
            return context_action::next;
        }
    };

    export struct [[nodiscard]] keydown {
        ev_type   type = EV_MAX;
        code_type code = KEY_MAX;

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) const noexcept {
            auto const& event = ctx.event();
            return (event.type() == type || type == EV_MAX) && (event.code() == code || code == KEY_MAX) &&
                   event.value() == 1;
        }
    };

    export struct [[nodiscard]] keyup {
        ev_type   type = EV_MAX;
        code_type code = KEY_MAX;

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) const noexcept {
            auto const& event = ctx.event();
            return (event.type() == type || type == EV_MAX) && (event.code() == code || code == KEY_MAX) &&
                   event.value() == 0;
        }
    };

    export struct [[nodiscard]] pressed {
        code_type code = KEY_MAX;

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) const noexcept {
            static_assert(has_mod<basic_keys_status, CtxT>, "We need keys_status to be in the pipeline.");
            auto const& keys = ctx.mod(keys_status);
            return keys.is_pressed(code);
        }
    };

    export template <typename... Funcs>
        requires(std::is_nothrow_copy_constructible_v<Funcs> && ...)
    struct or_op;

    export template <typename... Funcs>
        requires(std::is_nothrow_copy_constructible_v<Funcs> && ...)
    struct and_op {
      private:
        std::tuple<std::remove_cvref_t<Funcs>...> funcs;

      public:
        template <typename... InpFuncs>
            requires((std::convertible_to<InpFuncs, Funcs> && ...))
        explicit(false) constexpr and_op(InpFuncs&&... inp_funcs) noexcept
          : funcs{std::forward<InpFuncs>(inp_funcs)...} {}

        constexpr and_op(and_op const&) noexcept            = default;
        constexpr and_op& operator=(and_op const&) noexcept = default;
        constexpr and_op& operator=(and_op&&) noexcept      = default;
        constexpr and_op(and_op&&) noexcept                 = default;
        constexpr ~and_op() noexcept                        = default;

        template <typename Func>
        [[nodiscard]] consteval auto operator&(Func func) const noexcept {
            return std::apply(
              [func](auto const&... conds) {
                  return and_op<std::remove_cvref_t<Funcs>..., std::remove_cvref_t<Func>>{conds..., func};
              },
              funcs);
        }

        template <typename Func>
        [[nodiscard]] consteval auto operator|(Func func) const noexcept {
            return or_op<and_op, std::remove_cvref_t<Func>>{*this, func};
        }

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) noexcept {
            static_assert((std::is_nothrow_invocable_r_v<bool, Funcs, CtxT&> && ...), "All must be nothrow");
            return std::apply(
              [&ctx](auto&... cond) constexpr noexcept {
                  return (cond(ctx) && ... && true);
              },
              funcs);
        }
    };

    export template <typename... Funcs>
        requires(std::is_nothrow_copy_constructible_v<Funcs> && ...)
    struct or_op {
      private:
        std::tuple<std::remove_cvref_t<Funcs>...> funcs;

      public:
        template <typename... InpFuncs>
            requires((std::convertible_to<InpFuncs, Funcs> && ...))
        explicit(false) constexpr or_op(InpFuncs&&... inp_funcs) noexcept
          : funcs{std::forward<InpFuncs>(inp_funcs)...} {}

        constexpr or_op(or_op const&) noexcept            = default;
        constexpr or_op& operator=(or_op const&) noexcept = default;
        constexpr or_op& operator=(or_op&&) noexcept      = default;
        constexpr or_op(or_op&&) noexcept                 = default;
        constexpr ~or_op() noexcept                       = default;

        template <typename Func>
        [[nodiscard]] consteval auto operator&(Func func) const noexcept {
            return and_op<or_op, std::remove_cvref_t<Func>>{*this, func};
        }

        template <typename Func>
        [[nodiscard]] consteval auto operator|(Func func) const noexcept {
            return std::apply(
              [func](auto const&... conds) {
                  return or_op<std::remove_cvref_t<Funcs>..., std::remove_cvref_t<Func>>{conds..., func};
              },
              funcs);
        }

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) noexcept {
            static_assert((std::is_nothrow_invocable_r_v<bool, Funcs, CtxT&> && ...), "All must be nothrow");
            return std::apply(
              [&ctx](auto&... cond) constexpr noexcept {
                  return (cond(ctx) || ... || false);
              },
              funcs);
        }
    };

    export struct [[nodiscard]] basic_swipe {
      private:
        value_type x_axis = 0;
        value_type y_axis = 0;

        value_type x = 0;
        value_type y = 0;

      public:
        constexpr basic_swipe(value_type const inp_x_axis, value_type const inp_y_axis) noexcept
          : x_axis{inp_x_axis},
            y_axis{inp_y_axis} {}

        constexpr basic_swipe() noexcept                              = default;
        consteval basic_swipe(basic_swipe const&) noexcept            = default;
        consteval basic_swipe& operator=(basic_swipe const&) noexcept = default;
        constexpr basic_swipe(basic_swipe&&) noexcept                 = default;
        constexpr basic_swipe& operator=(basic_swipe&&) noexcept      = default;
        constexpr ~basic_swipe() noexcept                             = default;

        [[nodiscard]] constexpr bool operator()(Context auto& ctx) noexcept {
            using std::abs;
            using std::signbit;
            auto const& event = ctx.event();
            if (event.type() == EV_KEY && event.code() == BTN_LEFT) {
                x = 0;
                y = 0;
                return false;
            }
            if (is_mouse_movement(event)) {
                switch (event.code()) {
                    case REL_X: x += event.value(); break;
                    case REL_Y: y += event.value(); break;
                    default: break;
                }
                auto const res =
                  abs(x) >= abs(x_axis) && (signbit(x) == signbit(x_axis) || x_axis == 0) && // x axis
                  abs(y) >= abs(y_axis) && (signbit(y) == signbit(y_axis) || y_axis == 0);   // y axis
                if (res) {
                    x = 0;
                    y = 0;
                }
                return res;
            }
            return false;
        }
    };

    /// usage: op & pressed{...} | ...
    export constexpr and_op<> op;

    /// usage: on(released{...}, [] { ... })
    export constexpr basic_on<> on;

    export constexpr basic_swipe swipe_left{-200, 0};
    export constexpr basic_swipe swipe_right{200, 0};
    export constexpr basic_swipe swipe_up{0, -200};
    export constexpr basic_swipe swipe_down{0, 200};

} // namespace foresight
