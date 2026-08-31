// Created by moisrex on 7/14/25.

module;
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <libevdev/libevdev.h>
#include <linux/uinput.h>
#include <ranges>
#include <type_traits>
export module fs8.mods:router;
import fs8.devices.queries;
import fs8.context;
import :uinput;
import fs8.event;
import fs8.log;
import fs8.utils;
import fs8.nullable_indirect;
import :input_manager;
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

    // The routing table lives in the implementation unit: the hashes array is
    // ~16KiB and must not be copied inside every `basic_router` object. These
    // non-template helpers operate on the opaque `router_state` and are defined
    // in `mods/router.cxx`.
    namespace detail {
        struct router_state;

        void router_set_caps(nullable_indirect<router_state>& state, device_query const* queries_begin, std::size_t queries_count) noexcept;
        std::int32_t router_lookup(router_state& state, std::uint32_t hashed_value, bool is_syn_event) noexcept;
    } // namespace detail
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
            static_cast<void>(func(std::forward<Args>(args)...));
            return true;
        }
    }

    /**
     * This struct helps to pick which virtual device should be chosen as output based on the even type.
     */
    template <typename... Routes>
    struct [[nodiscard]] basic_router : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        static_assert((Modifier<Routes> && ...), "Bad routes");

      private:
        // equals to 9
        static constexpr std::uint16_t shift = std::bit_width<std::uint16_t>(KEY_MAX) - 1U;

        [[nodiscard]] static constexpr std::uint16_t hash(event_code const event) noexcept {
            return static_cast<std::uint16_t>(event.type << shift) | static_cast<std::uint16_t>(event.code);
        }

        // outputs
        std::array<device_query, sizeof...(Routes)> queries{};
        std::tuple<Routes...>                       routes;

        // the routing state (hashes + last picked index). The ~16KiB hashes
        // table lives in the implementation unit, allocated lazily at start.
        nullable_indirect<detail::router_state> state{};

      public:
        template <typename... C>
        consteval explicit basic_router(route<C>&&... inp_routes) noexcept
          : queries{inp_routes.query...},
            routes{std::move(inp_routes.pipeline)...} {
            static_assert((std::is_nothrow_move_constructible_v<Routes> && ...), "Make it consteval copyable.");
        }

        constexpr void set_caps() noexcept {
            detail::router_set_caps(state, queries.data(), queries.size());
        }

        /// The pipelines of the routes, exposed for recursion into this router.
        [[nodiscard]] constexpr auto sub_mods() noexcept {
            return std::apply(
              [](auto&... cur_routes) noexcept {
                  return std::tuple<decltype(cur_routes)&...>{cur_routes...};
              },
              routes);
        }

        template <typename... C>
            requires(sizeof...(C) >= 1)
        consteval auto operator[](route<C>... inp_routes) const noexcept {
            return basic_router<std::remove_cvref_t<C>...>{std::move(inp_routes)...};
        }

        bool emit(event_type const& event) noexcept {
            return operator()(event) == context_action::next;
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
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
            if (tag.code != special_start.code) {
                return context_action::drop_event;
            }
            set_caps();
            bool        is_init = true;
            std::size_t index   = 0;
            auto const  run_one = [&]<typename Route>(Route& route) {
                device_query const& cur_query  = queries[index];
                bool                init_valid = false;

                if constexpr (requires { route(ctx, cur_query, special_start); }) {
                    init_valid = invoke_bool(route, ctx, cur_query, special_start);
                } else if constexpr (requires { route(cur_query, special_start); }) {
                    init_valid = invoke_bool(route, cur_query, special_start);
                } else if constexpr (requires(dev_caps_view caps_view) { route(ctx, caps_view, special_start); }) {
                    init_valid = invoke_bool(route, ctx, cur_query.caps, special_start);
                } else if constexpr (requires(dev_caps_view caps_view) { route(caps_view, special_start); }) {
                    init_valid = invoke_bool(route, cur_query.caps, special_start);
                } else if constexpr (requires { route(ctx, special_start); }) {
                    init_valid = invoke_bool(route, ctx, special_start);
                } else if constexpr (requires { route.init(); }) {
                    init_valid = invoke_bool(route, special_start);
                } else {
                    // Intentionally Ignored since most mods don't need init.
                    init_valid = true;
                }

                ++index;
                is_init = is_init && init_valid;
            };
            std::apply(
              [&](auto&... all_routes) {
                  (run_one(all_routes), ...);
              },
              routes);
            if (!is_init) [[unlikely]] {
                log("Router failed to start at least one of the routes.");
                return context_action::idle;
            }
            return context_action::next;
        }

        context_action operator()(Context auto& ctx) noexcept {
            auto const& event        = ctx.event();
            auto const  hashed_value = hash(static_cast<event_code>(event));
            if (state.get() == nullptr) [[unlikely]] {
                return context_action::next;
            }
            auto const index = detail::router_lookup(*state, hashed_value, is_syn(event));
            if (index < 0) [[unlikely]] {
                return context_action::drop_event;
            }
            // log("Index: {} {}", index, event.code_name());
            return visit_at(routes, static_cast<std::size_t>(index), [&](auto& route) {
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
