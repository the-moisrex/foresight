// Created by moisrex on 7/14/25.

module;
#include <cassert>
#include <libevdev/libevdev.h>
#include <linux/uinput.h>
#include <print>
#include <ranges>
#include <type_traits>
export module foresight.mods.router;
import foresight.mods.caps;
import foresight.mods.context;
import foresight.devices.uinput;
import foresight.mods.event;
import foresight.main.log;
import foresight.main.utils;
import foresight.mods.intercept;

namespace foresight {

    template <std::size_t I>
    struct visit_impl {
        template <typename T, typename F>
        static constexpr decltype(auto) visit(T& tup, std::size_t idx, F&& fun) {
            if (idx == I - 1) {
                return std::forward<F>(fun)(std::get<I - 1>(tup));
            }
            return visit_impl<I - 1>::visit(tup, idx, fun);
        }
    };

    template <>
    struct visit_impl<0> {
        template <typename T, typename F>
        static constexpr decltype(auto)
        visit([[maybe_unused]] T& tup, [[maybe_unused]] std::size_t, [[maybe_unused]] F&& fun) {
            assert(false);

            // just to make the return type the same as the others:
            return std::forward<F>(fun)(std::get<0>(tup));
        }
    };

    template <typename F, typename... Ts>
    constexpr decltype(auto) visit_at(std::tuple<Ts...> const& tup, std::size_t idx, F&& fun) {
        return visit_impl<sizeof...(Ts)>::visit(tup, idx, std::forward<F>(fun));
    }

    template <typename F, typename... Ts>
    constexpr decltype(auto) visit_at(std::tuple<Ts...>& tup, std::size_t idx, F&& fun) {
        return visit_impl<sizeof...(Ts)>::visit(tup, idx, std::forward<F>(fun));
    }
} // namespace foresight

export namespace foresight {

    template <typename T>
    struct route {
        using route_type = std::remove_cvref_t<T>;

        dev_caps_view caps;
        route_type    mod;
    };

    template <typename T>
    route(dev_caps_view, T) -> route<std::remove_cvref_t<T>>;

    template <typename T>
    [[nodiscard]] consteval auto operator>>(dev_caps_view lhs, T&& rhs) noexcept {
        return route<std::remove_cvref_t<T>>{
          .caps = lhs,
          .mod  = std::forward<T>(rhs),
        };
    }

    /**
     * This struct helps to pick which virtual device should be chosen as output based on the even type.
     */
    template <typename... Routes>
    struct [[nodiscard]] basic_router {
        using ev_type    = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        // equals to 9
        static constexpr std::uint16_t shift = std::bit_width<std::uint16_t>(KEY_MAX) - 1U;

        [[nodiscard]] static constexpr std::uint16_t hash(event_code const event) noexcept {
            return static_cast<std::uint16_t>(event.type << shift) | static_cast<std::uint16_t>(event.code);
        }

        // returns 16127 or 0x3EFF
        static constexpr std::uint16_t max_hash = hash({.type = EV_MAX, .code = KEY_MAX});


        // the size is ~15KiB
        std::array<std::int8_t, max_hash> hashes{};

        // outputs
        std::array<dev_caps_view, sizeof...(Routes)> caps{};
        std::tuple<Routes...>                        routes;

        std::uint8_t last_index = 0;

      public:
        template <typename... C>
        consteval explicit basic_router(route<C>&&... inp_routes) noexcept(
          (std::is_nothrow_move_constructible_v<Routes> && ...))
          : caps{inp_routes.caps...},
            routes{std::move(inp_routes.mod)...} {
            // set_caps(inp_routes.caps...);
        }

        basic_router() noexcept((std::is_nothrow_default_constructible_v<Routes> && ...)) = default;
        basic_router(basic_router const&) noexcept(
          (std::is_nothrow_copy_constructible_v<Routes> && ...)) = default;
        basic_router(basic_router&&) noexcept(
          (std::is_nothrow_move_constructible_v<Routes> && ...)) = default;
        basic_router& operator=(basic_router const&) noexcept(
          (std::is_nothrow_copy_assignable_v<Routes> && ...)) = default;
        basic_router& operator=(basic_router&&) noexcept(
          (std::is_nothrow_move_assignable_v<Routes> && ...))                     = default;
        ~basic_router() noexcept((std::is_nothrow_destructible_v<Routes> && ...)) = default;

        /// Pass-through the init
        template <Context CtxT>
        constexpr context_action operator()(CtxT& ctx, start_tag) {
            set_caps();
            [&]<std::size_t... I>(std::index_sequence<I...>) constexpr {
                (([&]<typename Func>(Func& route) constexpr {
                     if constexpr (requires(dev_caps_view caps_view) { route(ctx, caps_view, start); }) {
                         route(ctx, caps[I], start);
                     } else if constexpr (requires(dev_caps_view caps_view) { route(caps_view, start); }) {
                         route(caps[I], start);
                     } else if constexpr (requires(dev_caps_view caps_view) { route(ctx, start); }) {
                         route(ctx, start);
                     } else if constexpr (requires { route.init(); }) {
                         route(start);
                     } else {
                         // Intentionally Ignored since most mods don't need init.
                     }
                 }(get<I>(routes))),
                 ...);
            }(std::make_index_sequence<sizeof...(Routes)>{});
            return context_action::next;
        }

        // template <typename... C>
        //     requires(sizeof...(C) >= 1 && (std::convertible_to<C, dev_caps_view> && ...))
        // constexpr void set_caps(C const&... caps_views) noexcept {
        //     hashes.fill(-1);
        //
        //     // Declaring which hash belongs to which uinput device
        //     (
        //       [this, input_pick = static_cast<std::int8_t>(0)](dev_caps_view const caps_view) mutable {
        //           for (auto const [type, codes, addition] : caps_view) {
        //               for (auto const code : codes) {
        //                   auto const index = hash({.type = type, .code = code});
        //                   if (addition /* && hashes.at(index) == -1 */) {
        //                       hashes.at(index) = input_pick;
        //                   }
        //               }
        //           }
        //           ++input_pick;
        //       }(caps_views),
        //       ...);
        // }

        constexpr void set_caps() noexcept {
            hashes.fill(-1);

            // Declaring which hash belongs to which uinput device
            std::int8_t input_pick = 0;
            for (auto const& cap_view : caps) {
                for (auto const [type, codes, action] : cap_view) {
                    for (auto const code : codes) {
                        auto const index = hash({.type = type, .code = code});
                        if (action == caps_action::append /* && hashes.at(index) == -1 */) {
                            hashes.at(index) = input_pick;
                        }
                    }
                }
                ++input_pick;
            }
        }

        template <std::ranges::input_range R>
            requires std::convertible_to<std::ranges::range_value_t<R>, evdev>
        void set_uinputs_from(R&& devs) {
            auto dev_iter = std::ranges::begin(devs);
            for (auto& udev : uinput_devices()) {
                if (dev_iter == std::ranges::end(devs)) {
                    break;
                }
                udev.set_device(*dev_iter++);
            }
        }

        void operator()(auto&&, tag auto) = delete;
        void operator()(tag auto)         = delete;

        template <typename... C>
            requires(sizeof...(C) > 1)
        consteval auto operator()(route<C>... inp_routes) const noexcept {
            return basic_router<std::remove_cvref_t<C>...>{std::move(inp_routes)...};
        }

        bool emit(ev_type const type, code_type const code, value_type const value) noexcept {
            return emit(event_type{type, code, value});
        }

        bool emit(input_event const& event) noexcept {
            return emit(event_type{event});
        }

        bool emit(event_type const& event) noexcept {
            return operator()(event) == context_action::next;
        }

        bool emit_syn() noexcept {
            return emit(syn());
        }

        template <typename RouteType>
        [[nodiscard]] constexpr auto routes_of() noexcept {
            return std::apply(
              [](auto&... cur_routes) {
                  return std::views::concat(std::views::single([]<typename T>(T& route) constexpr noexcept {
                             if constexpr (std::convertible_to<T, RouteType>) {
                                 return &route;
                             } else {
                                 return nullptr;
                             }
                         }(cur_routes))...)

                         // exclude non-RouteTypes
                         | std::views::filter([](auto* route) constexpr noexcept {
                               return route != nullptr;
                           })

                         // convert to reference
                         | std::views::transform([](RouteType* route) constexpr noexcept -> RouteType& {
                               return *route;
                           });
              },
              routes);
        }

        [[nodiscard]] constexpr auto uinput_devices() noexcept {
            return routes_of<basic_uinput>();
        }

        template <std::ranges::sized_range R, typename Func = basic_noop>
        void init_from(R&& devs, Func&& func = {}) noexcept {
            auto udevs = uinput_devices();
            for (auto&& [dev, udev] : std::views::zip(std::forward<R>(devs), udevs)) {
                udev.set_device(dev);
                if constexpr (std::invocable<Func, evdev&, basic_uinput&>) {
                    func(dev, udev);
                } else if constexpr (std::invocable<Func, basic_uinput&>) {
                    func(udev);
                } else if constexpr (std::invocable<Func, evdev const&>) {
                    func(dev);
                }
            }

            // Set up an empty device
            for (basic_uinput& udev : udevs | std::views::drop(devs.size())) {
                udev.set_device();
                if constexpr (std::invocable<Func, basic_uinput&>) {
                    func(udev);
                }
            }
        }

        void init_from_intercepted_devices(Context auto& pipeline) noexcept {
            init_from(pipeline.mod(intercept).devices());
        }

        context_action operator()(Context auto& ctx) noexcept {
            auto const& event        = ctx.event();
            auto const  hashed_value = hash(static_cast<event_code>(event));
            last_index = is_syn(event) ? last_index : static_cast<std::uint8_t>(hashes.at(hashed_value));
            if (last_index < 0) [[unlikely]] {
                log("Ignored ({}|{}): {} {} {}",
                    last_index,
                    hashed_value,
                    event.type_name(),
                    event.code_name(),
                    event.value());
                return context_action::ignore_event;
            }
            // log("Index: {} {}", last_index, event.code_name());
            return visit_at(routes, last_index, [&](auto& route) {
                return invoke_mod(route, ctx);
            });
        }
    };

    constexpr basic_router<> router;

    static_assert(OutputModifier<basic_router<>>, "Must be an output modifier.");

} // namespace foresight
