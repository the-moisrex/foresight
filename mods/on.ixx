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
        bool                                       was_active = false;

      public:
        constexpr basic_on() noexcept = default;

        template <typename InpCond, typename... InpFunc>
            requires(std::convertible_to<InpCond, CondT>
                     && (sizeof...(InpFunc) >= 1)
                     && !Context<InpCond>
                     && (!Context<InpFunc> && ...))
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

        // todo: should we propagate these to sub-on conditions as well?
        void operator()(auto&&, tag auto) = delete;

        template <typename NCondT, typename... NFuncs>
            requires(sizeof...(NFuncs) >= 1 && !Context<NCondT> && (!Context<NFuncs> && ...))
        consteval auto operator()(NCondT&& n_cond, NFuncs&&... n_funcs) const noexcept {
            return basic_on<std::remove_cvref_t<NCondT>, std::remove_cvref_t<NFuncs>...>{
              std::forward<NCondT>(n_cond),
              std::forward<NFuncs>(n_funcs)...};
        }

        template <typename NCondT, Context CtxT>
            requires(!Context<NCondT>)
        consteval auto operator()(NCondT&& n_cond, CtxT&& ctx) const noexcept {
            return std::apply(
              [&]<typename... ModT>(ModT&... mods) constexpr noexcept {
                  return basic_on<std::remove_cvref_t<NCondT>, std::remove_cvref_t<ModT>...>{
                    std::forward<NCondT>(n_cond),
                    mods...};
              },
              ctx.get_mods());
        }

        constexpr void init(Context auto& ctx) {
            invoke_init(ctx, funcs);
        }

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            bool const is_active   = invoke_cond(cond, ctx);
            bool const is_switched = is_active != std::exchange(was_active, is_active);
            if (!is_active) {
                if (is_switched) {
                    return invoke_mods(ctx, funcs, done);
                }
                return next;
            }
            if (is_switched && invoke_mods(ctx, funcs, start) == exit) {
                return exit;
            }
            return invoke_mods(ctx, funcs);
        }
    };

    export template <template <std::size_t> typename A, std::size_t N>
    struct [[nodiscard]] basic_code_adaptor {
      protected:
        std::array<code_type, N> codes{};

      public:
        constexpr basic_code_adaptor() noexcept                                     = default;
        consteval basic_code_adaptor(basic_code_adaptor const&) noexcept            = default;
        consteval basic_code_adaptor& operator=(basic_code_adaptor const&) noexcept = default;
        constexpr basic_code_adaptor(basic_code_adaptor&&) noexcept                 = default;
        constexpr basic_code_adaptor& operator=(basic_code_adaptor&&) noexcept      = default;

        explicit constexpr basic_code_adaptor(code_type const code) noexcept
            requires(N == 1)
          : codes{{code}} {}

        explicit constexpr basic_code_adaptor(std::array<code_type, N> const& inp_codes) noexcept
          : codes{inp_codes} {}

        explicit constexpr basic_code_adaptor(std::array<code_type, N>&& inp_codes) noexcept
          : codes{std::move(inp_codes)} {}

        template <typename... T>
            requires((std::convertible_to<T, code_type> && ...))
        consteval auto operator()(T... inp_codes) const noexcept {
            return A<sizeof...(T)>{std::array<code_type, sizeof...(T)>{static_cast<code_type>(inp_codes)...}};
        }

        template <std::size_t NN>
        consteval auto operator()(std::array<code_type, NN> const& inp_codes) const noexcept {
            return A<NN>{inp_codes};
        }
    };

    export template <std::size_t N>
    struct [[nodiscard]] basic_pressed : basic_code_adaptor<basic_pressed, N> {
        using basic_code_adaptor<basic_pressed, N>::basic_code_adaptor;
        using basic_code_adaptor<basic_pressed, N>::operator();

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) const noexcept {
            static_assert(has_mod<basic_keys_status, CtxT>, "We need keys_status to be in the pipeline.");
            return ctx.mod(keys_status).is_pressed(this->codes);
        }
    };

    export constexpr basic_pressed<0> pressed;

    export struct [[nodiscard]] keydown {
        code_type code = KEY_MAX;

        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.is(EV_KEY, code, 1);
        }
    };

    export struct [[nodiscard]] keyup {
        code_type code = KEY_MAX;

        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.is(EV_KEY, code, 0);
        }
    };

    export template <typename Func>
    struct [[nodiscard]] op_not {
        [[no_unique_address]] Func func;

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) noexcept {
            return !invoke_cond(func, ctx);
        }
    };

    export template <typename FuncT>
    struct [[nodiscard]] basic_longtime_released {
      private:
        [[no_unique_address]] FuncT func{};
        std::chrono::microseconds   dur = std::chrono::milliseconds{100};
        std::chrono::microseconds   last_time{};

      public:
        constexpr basic_longtime_released() noexcept = default;

        constexpr explicit basic_longtime_released(FuncT const&                    inp_func,
                                                   std::chrono::microseconds const inp_dur) noexcept
          : func{inp_func},
            dur{inp_dur} {}

        consteval basic_longtime_released(basic_longtime_released const&) noexcept            = default;
        consteval basic_longtime_released& operator=(basic_longtime_released const&) noexcept = default;
        constexpr basic_longtime_released(basic_longtime_released&&) noexcept                 = default;
        constexpr basic_longtime_released& operator=(basic_longtime_released&&) noexcept      = default;
        constexpr ~basic_longtime_released() noexcept                                         = default;

        // todo: initialize the dur with repetition delay of the keyboard

        template <typename InpFuncT>
        consteval auto operator()(InpFuncT&&                      inp_func,
                                  std::chrono::microseconds const inp_dur) const noexcept {
            return basic_longtime_released<std::remove_cvref_t<InpFuncT>>{std::forward<InpFuncT>(inp_func),
                                                                          inp_dur};
        }

        template <typename InpFuncT>
        consteval auto operator()(InpFuncT&& inp_func) const noexcept {
            return basic_longtime_released<std::remove_cvref_t<InpFuncT>>{std::forward<InpFuncT>(inp_func),
                                                                          dur};
        }

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) noexcept {
            if (invoke_cond(func, ctx)) {
                if (last_time == std::chrono::microseconds(0)) {
                    last_time = ctx.event().micro_time();
                }
            } else {
                if (last_time == std::chrono::microseconds(0)) {
                    return false;
                }
                return (ctx.event().micro_time() - std::exchange(last_time, std::chrono::microseconds(0)))
                       >= dur;
            }
            return false;
        }
    };

    export constexpr basic_longtime_released<basic_noop> longtime_released;

    export template <typename CondT = basic_noop>
    struct [[nodiscard]] basic_limit_mouse_travel {
      private:
        value_type x_amount = 50;
        value_type y_amount = 50;

        value_type x_cur = 0;
        value_type y_cur = 0;

        [[no_unique_address]] CondT cond{};

      public:
        constexpr basic_limit_mouse_travel() noexcept = default;

        constexpr explicit basic_limit_mouse_travel(
          CondT const&     inp_cond,
          value_type const x,
          value_type const y) noexcept
          : x_amount{x},
            y_amount{y},
            cond{inp_cond} {}

        constexpr explicit basic_limit_mouse_travel(value_type const x, value_type const y) noexcept
          : x_amount{x},
            y_amount{y} {}

        constexpr explicit basic_limit_mouse_travel(value_type const both) noexcept
          : x_amount{both},
            y_amount{both} {}

        consteval basic_limit_mouse_travel(basic_limit_mouse_travel const&) noexcept            = default;
        consteval basic_limit_mouse_travel& operator=(basic_limit_mouse_travel const&) noexcept = default;
        constexpr basic_limit_mouse_travel(basic_limit_mouse_travel&&) noexcept                 = default;
        constexpr basic_limit_mouse_travel& operator=(basic_limit_mouse_travel&&) noexcept      = default;
        constexpr ~basic_limit_mouse_travel() noexcept                                          = default;

        consteval basic_limit_mouse_travel operator()(value_type const x, value_type const y) const noexcept {
            return basic_limit_mouse_travel{x, y};
        }

        consteval basic_limit_mouse_travel operator()(value_type const both) const noexcept {
            return basic_limit_mouse_travel{both};
        }

        template <typename InpCondT>
        consteval auto
        operator()(InpCondT&& inp_cond, value_type const x, value_type const y) const noexcept {
            return basic_limit_mouse_travel<std::remove_cvref_t<InpCondT>>{
              std::forward<InpCondT>(inp_cond),
              x,
              y};
        }

        template <typename InpCondT>
        consteval auto operator()(InpCondT&& inp_cond, value_type const both) const noexcept {
            return basic_limit_mouse_travel<std::remove_cvref_t<InpCondT>>{
              std::forward<InpCondT>(inp_cond),
              both,
              both};
        }

        [[nodiscard]] constexpr bool operator()(Context auto& ctx) noexcept {
            auto const& event       = ctx.event();
            bool const  is_it_mouse = is_mouse_movement(event);
            if (invoke_cond(cond, ctx)) {
                if (is_it_mouse && (x_cur == -1 || y_cur == -1)) {
                    switch (event.code()) {
                        case REL_X: x_cur = event.value(); break;
                        case REL_Y: y_cur = event.value(); break;
                        default: break;
                    }
                    return true;
                }
            } else {
                x_cur = -1;
                y_cur = -1;
                return false;
            }
            if (is_it_mouse) {
                switch (event.code()) {
                    case REL_X: x_cur += std::abs(event.value()); break;
                    case REL_Y: y_cur += std::abs(event.value()); break;
                    default: break;
                }
            }
            // log("{} {} {}", x_cur, y_cur, std::abs(x_cur) <= x_amount && std::abs(y_cur) <= y_amount);
            return (x_cur <= x_amount) && (y_cur <= y_amount);
        }
    };

    export constexpr basic_limit_mouse_travel<> limit_mouse_travel;

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
        [[nodiscard]] bool operator()(CtxT& ctx) const noexcept {
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

        consteval and_op(and_op const&) noexcept            = default;
        consteval and_op& operator=(and_op const&) noexcept = default;
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
            if constexpr (sizeof...(Funcs) == 0) {
                return or_op<std::remove_cvref_t<Func>>{func};
            } else {
                return or_op<and_op, std::remove_cvref_t<Func>>{*this, func};
            }
        }

        [[nodiscard]] constexpr bool operator()(Context auto& ctx) noexcept {
            return std::apply(
              [&ctx](auto&... cond) constexpr noexcept {
                  return (invoke_cond(cond, ctx) && ...);
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
            if constexpr (sizeof...(Funcs) == 0) {
                return and_op<std::remove_cvref_t<Func>>{func};
            } else {
                return and_op<or_op, std::remove_cvref_t<Func>>{*this, func};
            }
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
                  return (invoke_cond(cond, ctx) || ...);
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

        void reset() noexcept;

        [[nodiscard]] constexpr value_type x() const noexcept {
            return cur_x;
        }

        [[nodiscard]] constexpr value_type y() const noexcept {
            return cur_y;
        }

        [[nodiscard]] bool is_active(value_type x_axis, value_type y_axis) const noexcept;

        /// Returns the number of times X and Y have passed
        /// multiples of their respective thresholds.
        /// Returns a std::pair where .first is x_multiples_passed and .second is y_multiples_passed.
        [[nodiscard]] std::pair<std::uint16_t, std::uint16_t> passed_threshold_count(
          value_type x_axis,
          value_type y_axis) const noexcept;

        void operator()(event_type const& event) noexcept;
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
        [[nodiscard]] bool operator()(CtxT& ctx) noexcept {
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

        [[nodiscard]] bool operator()(event_type const& event) noexcept;
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
