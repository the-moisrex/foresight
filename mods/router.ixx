// Created by moisrex on 7/14/25.

module;
#include <cassert>
#include <libevdev/libevdev.h>
#include <linux/uinput.h>
#include <ranges>
#include <type_traits>
export module fs8.mods.router;
import fs8.devices.queries;
import fs8.context;
import fs8.devices.uinput;
import fs8.event;
import fs8.log;
import fs8.utils;
import fs8.mods.input_manager;
import fs8.traits;

namespace fs8 {

    template <std::size_t I>
    struct visit_impl {
        template <typename T, typename F>
        static constexpr decltype(auto) visit(T& tup, std::size_t idx, F&& fun) noexcept {
            if (idx == I - 1) {
                return std::forward<F>(fun)(std::get<I - 1>(tup));
            }
            return visit_impl<I - 1>::visit(tup, idx, fun);
        }
    };

    template <>
    struct visit_impl<0> {
        template <typename T, typename F>
        static constexpr decltype(auto) visit([[maybe_unused]] T& tup, [[maybe_unused]] std::size_t, [[maybe_unused]] F&& fun) noexcept {
            assert(false);

            // just to make the return type the same as the others:
            return std::forward<F>(fun)(std::get<0>(tup));
        }
    };

    template <typename F, typename... Ts>
    constexpr decltype(auto) visit_at(std::tuple<Ts...> const& tup, std::size_t idx, F&& fun) noexcept {
        return visit_impl<sizeof...(Ts)>::visit(tup, idx, std::forward<F>(fun));
    }

    template <typename F, typename... Ts>
    constexpr decltype(auto) visit_at(std::tuple<Ts...>& tup, std::size_t idx, F&& fun) noexcept {
        return visit_impl<sizeof...(Ts)>::visit(tup, idx, std::forward<F>(fun));
    }
} // namespace fs8

export namespace fs8 {

    template <typename T>
    struct [[nodiscard]] route {
        using route_type = std::remove_cvref_t<T>;

        device_query query;
        route_type   pipeline;
    };

    template <typename T>
    route(device_query, T) -> route<std::remove_cvref_t<T>>;

    template <typename T>
    [[nodiscard]] consteval auto operator>>(device_query const lhs, T&& rhs) noexcept {
        return route<std::remove_cvref_t<T>>{
          .query    = lhs,
          .pipeline = std::forward<T>(rhs),
        };
    }

    template <typename T>
    [[nodiscard]] consteval auto operator>>(dev_caps_view const lhs, T&& rhs) noexcept {
        return route<std::remove_cvref_t<T>>{
          .query    = query | lhs,
          .pipeline = std::forward<T>(rhs),
        };
    }

    template <typename FuncT, typename... Args>
    [[nodiscard]] bool invoke_bool(FuncT& func, Args&&... args) noexcept {
        static_assert(std::is_nothrow_invocable_v<FuncT, Args...>, "Mark the mod as nothrow.");
        if constexpr (std::convertible_to<bool, std::invoke_result_t<FuncT, Args...>>) {
            return func(std::forward<Args>(args)...);
        } else {
            func(std::forward<Args>(args)...);
            return true;
        }
    }

    /**
     * This struct helps to pick which virtual device should be chosen as output based on the even type.
     */
    template <typename... Routes>
    struct [[nodiscard]] basic_router : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using ev_type    = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

        static_assert((Modifier<Routes> && ...), "Bad routes");

      private:
        // equals to 9
        static constexpr std::uint16_t shift = std::bit_width<std::uint16_t>(KEY_MAX) - 1U;

        [[nodiscard]] static constexpr std::uint16_t hash(event_code const event) noexcept {
            return static_cast<std::uint16_t>(event.type << shift) | static_cast<std::uint16_t>(event.code);
        }

        // returns 16127 or 0x3EFF
        static constexpr std::uint16_t max_hash = hash({.type = EV_MAX, .code = KEY_MAX});


        // the size is ~15KiB
        // todo: use pimpl and move this into implementation
        std::array<std::int8_t, max_hash> hashes{};

        // outputs
        std::array<device_query, sizeof...(Routes)> queries{};
        std::tuple<Routes...>                       routes;

        std::uint8_t last_index = 0;

      public:
        template <typename... C>
        consteval explicit basic_router(route<C>&&... inp_routes) noexcept
          : queries{inp_routes.query...},
            routes{std::move(inp_routes.pipeline)...} {
            static_assert((std::is_nothrow_move_constructible_v<Routes> && ...), "Make it consteval copyable.");
        }

        constexpr void set_caps() noexcept {
            hashes.fill(-1);

            // Declaring which hash belongs to which uinput device
            std::int8_t input_pick = 0;
            for (device_query const& cur_query : queries) {
                for (auto const [type, codes, action] : cur_query.caps) {
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

        void operator()(auto&&, Tag auto) = delete;
        void operator()(Tag auto)         = delete;

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

        // template <typename RouteType>
        // [[nodiscard]] constexpr auto routes_of() noexcept {
        //     return std::apply(
        //       [](auto&... cur_routes) {
        //           return std::views::concat(std::views::single([]<typename T>(T& route) constexpr noexcept {
        //                      if constexpr (std::convertible_to<T, RouteType>) {
        //                          return &route;
        //                      } else {
        //                          return nullptr;
        //                      }
        //                  }(cur_routes))...)
        //
        //                  // exclude non-RouteTypes
        //                  | std::views::filter([](auto* route) constexpr noexcept {
        //                        return route != nullptr;
        //                    })
        //
        //                  // convert to reference
        //                  | std::views::transform([](RouteType* route) constexpr noexcept -> RouteType& {
        //                        return *route;
        //                    });
        //       },
        //       routes);
        // }

        // [[nodiscard]] constexpr auto uinput_devices() noexcept {
        //     return routes_of<basic_uinput>();
        // }

        // template <std::ranges::input_range R>
        //     requires std::convertible_to<std::ranges::range_value_t<R>, evdev>
        // void set_uinputs_from(R&& devs) {
        //     auto dev_iter = std::ranges::begin(devs);
        //     for (auto& vdev : uinput_devices()) {
        //         if (dev_iter == std::ranges::end(devs)) {
        //             break;
        //         }
        //         vdev.set_device(*dev_iter++);
        //     }
        // }

        // template <std::ranges::sized_range R, typename Func = basic_noop>
        // void init_from(R&& devs, Func&& func = {}) noexcept {
        //     auto vdevs = uinput_devices();
        //     for (auto&& [dev, vdev] : std::views::zip(std::forward<R>(devs), vdevs)) {
        //         vdev.set_device(dev);
        //         if constexpr (std::invocable<Func, evdev&, basic_uinput&>) {
        //             func(dev, vdev);
        //         } else if constexpr (std::invocable<Func, basic_uinput&>) {
        //             func(vdev);
        //         } else if constexpr (std::invocable<Func, evdev const&>) {
        //             func(dev);
        //         }
        //     }
        //
        //     // Set up an empty device
        //     for (basic_uinput& vdev : vdevs | std::views::drop(devs.size())) {
        //         vdev.set_device();
        //         if constexpr (std::invocable<Func, basic_uinput&>) {
        //             func(vdev);
        //         }
        //     }
        // }

        // void init_from_intercepted_devices(Context auto& pipeline) noexcept {
        //     init_from(pipeline.mod(input_manager).devices());
        // }

        /// Pass-through the init
        template <Context CtxT>
        context_action operator()(CtxT& ctx, start_tag) noexcept {
            set_caps();
            auto cur_query = queries.begin();
            template for (auto& route : routes) {
                bool init_valid = false;

                if constexpr (requires { route(ctx, query, start); }) {
                    init_valid = invoke_bool(route, ctx, *cur_query, start);
                } else if constexpr (requires { route(query, start); }) {
                    init_valid = invoke_bool(route, *cur_query, start);
                } else if constexpr (requires(dev_caps_view caps_view) { route(ctx, caps_view, start); }) {
                    init_valid = invoke_bool(route, ctx, cur_query->caps, start);
                } else if constexpr (requires(dev_caps_view caps_view) { route(caps_view, start); }) {
                    init_valid = invoke_bool(route, cur_query->caps, start);
                } else if constexpr (requires { route(ctx, start); }) {
                    init_valid = invoke_bool(route, ctx, start);
                } else if constexpr (requires { route.init(); }) {
                    init_valid = invoke_bool(route, start);
                } else {
                    // Intentionally Ignored since most mods don't need init.
                    init_valid = true;
                }

                if (!init_valid) [[unlikely]] {
                    log("Router failed to start at least one of the routes.");
                    return context_action::idle;
                }
                ++cur_query;
            }
            return context_action::next;
        }

        context_action operator()(Context auto& ctx) noexcept {
            auto const& event        = ctx.event();
            auto const  hashed_value = hash(static_cast<event_code>(event));
            last_index               = is_syn(event) ? last_index : static_cast<std::uint8_t>(hashes.at(hashed_value));
            if (last_index < 0) [[unlikely]] {
                log("Ignored ({}|{}): {} {} {}", last_index, hashed_value, event.type_name(), event.code_name(), event.value());
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

    template <typename R>
    [[nodiscard]] bool is_ok(R&& vdevs) noexcept {
        bool ok = true;
        for (auto const& dev : vdevs) {
            ok &= dev.is_ok();
        }
        return ok;
    }

} // namespace fs8
