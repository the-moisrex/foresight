// Created by moisrex on 6/18/25.

module;
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <linux/input-event-codes.h>
#include <tuple>
#include <utility>
export module foresight.mods.on;

export import foresight.main.utils;
import foresight.mods.keys_status;
import foresight.mods.context;

namespace foresight {

    using ev_type    = event_type::type_type;
    using code_type  = event_type::code_type;
    using value_type = event_type::value_type;

    export constexpr struct [[nodiscard]] basic_always_enable {
        constexpr bool operator()() const noexcept {
            return true;
        }
    } always_enable;

    export constexpr struct [[nodiscard]] basic_always_disable {
        constexpr bool operator()() const noexcept {
            return true;
        }
    } always_disable;

    export template <typename CondT = basic_always_enable, typename... Funcs>
    struct [[nodiscard]] basic_on {
      private:
        [[no_unique_address]] CondT                cond;
        [[no_unique_address]] std::tuple<Funcs...> funcs;

      public:
        constexpr basic_on() noexcept = default;

        template <typename InpCond, typename... InpFunc>
            requires(std::convertible_to<InpCond, CondT>)
        explicit constexpr basic_on(InpCond&& inp_cond, InpFunc&&... inp_funcs) noexcept
          : cond{std::forward<InpCond>(inp_cond)},
            funcs{std::forward<InpFunc>(inp_funcs)...} {}

        template <typename InpCond>
            requires(std::convertible_to<InpCond, CondT>)
        explicit constexpr basic_on(InpCond&& inp_cond, std::tuple<Funcs...> const& inp_funcs) noexcept
          : cond{std::forward<InpCond>(inp_cond)},
            funcs{inp_funcs} {}

        consteval basic_on(basic_on const&)                = default;
        consteval basic_on& operator=(basic_on const&)     = default;
        constexpr basic_on(basic_on&&) noexcept            = default;
        constexpr basic_on& operator=(basic_on&&) noexcept = default;
        constexpr ~basic_on() noexcept                     = default;

        template <typename NCondT, typename... NFuncs>
        consteval auto operator()(NCondT&& n_cond, NFuncs&&... n_funcs) const noexcept {
            return basic_on<std::remove_cvref_t<NCondT>, std::remove_cvref_t<NFuncs>...>{
              std::forward<NCondT>(n_cond),
              std::forward<NFuncs>(n_funcs)...};
        }

        template <typename NCondT, Context CtxT>
        consteval auto operator()(NCondT&& n_cond, CtxT&& ctx) const noexcept {
            return std::apply(
              [&]<typename... ModT>(ModT&... mods) noexcept(CtxT::is_nothrow) {
                  return basic_on<std::remove_cvref_t<NCondT>, std::remove_cvref_t<ModT>...>{
                    std::forward<NCondT>(n_cond),
                    mods...};
              },
              ctx.get_mods());
        }

        // template <typename EvTempl, typename InpFunc, typename... Args>
        //     requires(sizeof...(Args) >= 1 && std::invocable<InpFunc, Args...>)
        // consteval auto operator()(EvTempl templ, InpFunc inp_func, Args&&... args) const noexcept {
        //     auto const cmd = [inp_func, args...]() constexpr noexcept {
        //         static_assert(std::is_nothrow_invocable_v<InpFunc, Args...>, "Make it nothrow");
        //         return std::invoke(inp_func, std::forward<Args>(args)...);
        //     };
        //     using cmd_type = std::remove_cvref_t<decltype(cmd)>;
        //     return basic_on<std::remove_cvref_t<EvTempl>, cmd_type>{templ, cmd};
        // }

        constexpr void init(Context auto& ctx) {
            invoke_init(ctx, funcs);
        }

        constexpr context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            if (!invoke_cond(cond, ctx)) {
                return next;
            }
            return invoke_mods(ctx, funcs);
        }
    };

    export struct [[nodiscard]] keydown {
        ev_type   type = EV_MAX;
        code_type code = KEY_MAX;

        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return (event.type() == type || type == EV_MAX)
                   && (event.code() == code || code == KEY_MAX)
                   && event.value()
                   == 1;
        }
    };

    export struct [[nodiscard]] keyup {
        ev_type   type = EV_MAX;
        code_type code = KEY_MAX;

        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return (event.type() == type || type == EV_MAX)
                   && (event.code() == code || code == KEY_MAX)
                   && event.value()
                   == 0;
        }
    };

    export struct [[nodiscard]] pressed {
        code_type code = KEY_MAX;

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) const noexcept {
            static_assert(has_mod<basic_keys_status, CtxT>, "We need keys_status to be in the pipeline.");
            return ctx.mod(keys_status).is_pressed(code);
        }
    };

    export struct [[nodiscard]] led_on {
        code_type code = LED_MAX;

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) const noexcept {
            static_assert(has_mod<basic_led_status, CtxT>, "We need keys_status to be in the pipeline.");
            return ctx.mod(led_status).is_on(code);
        }
    };

    export struct [[nodiscard]] led_off {
        code_type code = LED_MAX;

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) const noexcept {
            static_assert(has_mod<basic_led_status, CtxT>, "We need keys_status to be in the pipeline.");
            return ctx.mod(led_status).is_off(code);
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
            return std::apply(
              [&ctx](auto&... cond) constexpr noexcept {
                  return (invoke_cond(cond, ctx) && ... && true);
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
                  return (invoke_cond(cond, ctx) || ... || false);
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
            return abs(cur_x)
                   >= abs(x_axis)
                   && (signbit(cur_x) == signbit(x_axis) || x_axis == 0)
                   &&                                                     // X
                   abs(cur_y)
                   >= abs(y_axis)
                   && (signbit(cur_y) == signbit(y_axis) || y_axis == 0); // Y
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

        constexpr void operator()(event_type const& event) noexcept {
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

    export struct [[nodiscard]] basic_multi_click {
        using duration_type = std::chrono::microseconds;

      private:
        user_event    usr;
        std::uint8_t  count              = 2;
        duration_type duration_threshold = std::chrono::milliseconds{300};

        std::uint8_t  cur_count = 0;
        duration_type last_click{};

      public:
        constexpr explicit basic_multi_click(
          user_event const&   inp_usr_event,
          std::uint8_t const  inp_count     = 2,
          duration_type const dur_threshold = std::chrono::milliseconds{300}) noexcept
          : usr{inp_usr_event},
            count{inp_count},
            duration_threshold{dur_threshold} {}

        consteval basic_multi_click(basic_multi_click const&) noexcept            = default;
        consteval basic_multi_click& operator=(basic_multi_click const&) noexcept = default;
        constexpr basic_multi_click(basic_multi_click&&) noexcept                 = default;
        constexpr basic_multi_click& operator=(basic_multi_click&&) noexcept      = default;
        constexpr ~basic_multi_click() noexcept                                   = default;

        [[nodiscard]] constexpr bool operator()(event_type const& event) noexcept {
            auto const now = event.micro_time();
            if (event != usr) {
                return false;
            }
            if ((now - std::exchange(last_click, now)) > duration_threshold) {
                cur_count = 1;
                return false;
            }
            bool const res = ++cur_count >= count;
            if (res) {
                cur_count = 0;
            }
            return res;
        }
    };

    constexpr basic_on<> enable_only;

    /// usage: op & pressed{...} | ...
    export constexpr and_op<> op;

    /// usage: on(released{...}, [] { ... })
    export constexpr basic_on<> on;

    constexpr auto               no_axis           = std::numeric_limits<value_type>::max();
    constexpr value_type         default_sipe_step = 120;
    export constexpr basic_swipe swipe_left{-default_sipe_step, no_axis};
    export constexpr basic_swipe swipe_right{default_sipe_step, no_axis};
    export constexpr basic_swipe swipe_up{no_axis, -default_sipe_step};
    export constexpr basic_swipe swipe_down{no_axis, default_sipe_step};

    constexpr user_event               left_click{EV_KEY, BTN_LEFT, 0};
    export constexpr basic_multi_click double_click{left_click};
    export constexpr basic_multi_click triple_click{left_click, 3};
} // namespace foresight
