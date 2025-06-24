// Created by moisrex on 6/18/25.

module;
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <linux/input-event-codes.h>
#include <tuple>
#include <utility>
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
                return invoke_mod(func, ctx);
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

    export constexpr struct [[nodiscard]] basic_swipe_detector {
      private:
        value_type cur_x = 0;
        value_type cur_y = 0;

      public:
        constexpr basic_swipe_detector() noexcept                                       = default;
        consteval basic_swipe_detector(basic_swipe_detector const&) noexcept            = default;
        consteval basic_swipe_detector& operator=(basic_swipe_detector const&) noexcept = default;
        constexpr basic_swipe_detector(basic_swipe_detector&&) noexcept                 = default;
        constexpr basic_swipe_detector& operator=(basic_swipe_detector&&) noexcept      = default;
        constexpr ~basic_swipe_detector() noexcept                                      = default;

        constexpr void reset() noexcept {
            cur_x = 0;
            cur_y = 0;
        }

        [[nodiscard]] constexpr value_type x() const noexcept {
            return cur_x;
        }

        [[nodiscard]] constexpr value_type y() const noexcept {
            return cur_y;
        }

        [[nodiscard]] constexpr bool is_active(value_type const x_axis,
                                               value_type const y_axis) const noexcept {
            using std::abs;
            using std::signbit;
            return abs(cur_x) >= abs(x_axis) && (signbit(cur_x) == signbit(x_axis) || x_axis == 0) && // X
                   abs(cur_y) >= abs(y_axis) && (signbit(cur_y) == signbit(y_axis) || y_axis == 0);   // Y
        }

        /// Returns the number of times X and Y have passed
        /// multiples of their respective thresholds.
        /// Returns a std::pair where .first is x_multiples_passed and .second is y_multiples_passed.
        [[nodiscard]] constexpr std::pair<std::uint16_t, std::uint16_t> passed_threshold_count(
          value_type const x_axis,
          value_type const y_axis) const noexcept {
            using std::abs;
            using std::signbit; // Required for checking signs

            std::uint16_t x_multiples = 0;
            std::uint16_t y_multiples = 0;

            // Calculate multiples for X
            if (x_axis != 0) {
                // Check if cur_x and x_axis have the same sign (or if cur_x is zero)
                // If they do, then calculate multiples. Otherwise, count is 0.
                if (signbit(cur_x) == signbit(x_axis)) {
                    x_multiples = static_cast<std::uint16_t>(abs(cur_x) / abs(x_axis));
                }
            }
            // If x_axis is 0, x_multiples remains 0, which is correct.

            // Calculate multiples for Y
            if (y_axis != 0) {
                // Check if cur_y and y_axis have the same sign (or if cur_y is zero)
                if (signbit(cur_y) == signbit(y_axis)) {
                    y_multiples = static_cast<std::uint16_t>(abs(cur_y) / abs(y_axis));
                }
            }
            // If y_axis is 0, y_multiples remains 0, which is correct.

            return {x_multiples, y_multiples};
        }

        constexpr void operator()(Context auto& ctx) noexcept {
            auto const& event = ctx.event();
            if (event.type() == EV_KEY && event.code() == BTN_LEFT) {
                reset();
                return;
            }
            if (is_mouse_movement(event)) {
                switch (event.code()) {
                    case REL_X: cur_x += event.value(); break;
                    case REL_Y: cur_y += event.value(); break;
                    default: break;
                }
            }
        }
    } swipe_detector;

    export struct [[nodiscard]] basic_swipe {
      private:
        value_type x_axis = 0;
        value_type y_axis = 0;

        std::uint16_t count = 0;

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

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) noexcept {
            static_assert(has_mod<basic_swipe_detector, CtxT>, "You need to enable swipe detector");

            if (!is_mouse_movement(ctx.event())) {
                return false;
            }

            auto const [cur_x_count, cur_y_count] =
              ctx.mod(swipe_detector).passed_threshold_count(x_axis, y_axis);
            auto const cur_count = cur_x_count + cur_y_count;
            return cur_count > std::exchange(count, cur_count);
        }
    };

    /// usage: op & pressed{...} | ...
    export constexpr and_op<> op;

    /// usage: on(released{...}, [] { ... })
    export constexpr basic_on<> on;

    constexpr auto               no_axis           = std::numeric_limits<value_type>::max();
    constexpr value_type         default_sipe_step = 150;
    export constexpr basic_swipe swipe_left{-default_sipe_step, no_axis};
    export constexpr basic_swipe swipe_right{default_sipe_step, no_axis};
    export constexpr basic_swipe swipe_up{no_axis, -default_sipe_step};
    export constexpr basic_swipe swipe_down{no_axis, default_sipe_step};

} // namespace foresight
