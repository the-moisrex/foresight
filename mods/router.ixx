// Created by moisrex on 7/14/25.

module;
#include <cassert>
#include <linux/uinput.h>
#include <ranges>
#include <type_traits>
export module foresight.mods.router;
import foresight.mods.caps;
import foresight.mods.context;
import foresight.uinput;
import foresight.mods.event;

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
        using route_type = T;

        dev_caps_view caps;
        T             mod;
    };

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
        std::tuple<Routes...> routes;

      public:
        template <typename... C>
        consteval explicit basic_router(route<C>&&... inp_routes) noexcept
          : routes{std::move(inp_routes.mod)...} {
            set_caps(inp_routes.caps...);
        }

        basic_router() noexcept                               = default;
        basic_router(basic_router const&) noexcept            = default;
        basic_router(basic_router&&) noexcept                 = default;
        basic_router& operator=(basic_router const&) noexcept = default;
        basic_router& operator=(basic_router&&) noexcept      = default;
        ~basic_router() noexcept                              = default;

        // void init() {
        //     if constexpr (is_dynamic) {
        //         // todo
        //     } else {
        //         // regenerate dev_caps_view:
        //         std::array<dev_cap_view, EV_MAX> views{};
        //
        //         // todo: this includes some invalid types as well
        //         for (ev_type type = 0; type != EV_MAX; ++type) {
        //             views.at(type) = dev_cap_view{
        //               .type = type,
        //               .codes =
        //                 std::span<event_type::code_type const>{
        //                                                        std::next(hashes.begin(), hash({.type =
        //                                                        type, .code = 0})),
        //                                                        std::next(hashes.begin(), hash({.type =
        //                                                        type, .code = KEY_MAX}))},
        //             };
        //         }
        //
        //         // replace uinputs with the input devices with the highest percentage of matching:
        //         for (auto& cur_uinput : uinputs) {
        //             evdev_rank best{};
        //             for (evdev_rank cur : foresight::devices(views)) {
        //                 if (cur.match >= best.match) {
        //                     best = std::move(cur);
        //                 }
        //             }
        //             if (best.match != 0) {
        //                 cur_uinput = std::move(best);
        //             }
        //         }
        //     }
        // }

        template <typename... C>
            requires(std::convertible_to<C, dev_caps_view> && ...)
        constexpr void set_caps(C const&... caps_views) noexcept {
            hashes.fill(0);

            // Declaring which hash belongs to which uinput device
            (
              [this, input_pick = static_cast<std::int8_t>(0)](dev_caps_view const caps_view) mutable {
                  for (auto const [type, codes, addition] : caps_view) {
                      for (auto const code : codes) {
                          auto const index = hash({.type = type, .code = code});
                          hashes.at(index) = addition ? input_pick : -1;
                      }
                  }
                  ++input_pick;
              }(caps_views),
              ...);
        }

        // template <std::ranges::input_range R>
        //     requires std::convertible_to<std::ranges::range_value_t<R>, evdev>
        // void init_uinputs_from(R&& devs) {
        //     if constexpr (is_dynamic) {
        //         uinputs.clear();
        //         if constexpr (std::ranges::sized_range<R>) {
        //             uinputs.reserve(devs.size());
        //         }
        //         for (evdev const& dev : devs) {
        //             uinputs.emplace_back(dev);
        //         }
        //     } else {
        //         std::uint8_t index = 0;
        //         for (evdev const& dev : devs) {
        //             if (index >= uinputs.size()) {
        //                 throw std::invalid_argument("Too many devices has been given to us.");
        //             }
        //             uinputs.at(index++) = basic_uinput{dev};
        //             std::println("Dev: {} {} {}", dev.device_name(), uinputs.at(index - 1).syspath(),
        //             uinputs.at(index - 1).devnode());
        //         }
        //     }
        // }

        template <typename... C>
        consteval auto operator()(route<C>&&... inp_rotues) const noexcept {
            return basic_router<std::remove_cvref_t<C>...>{std::move(inp_rotues)...};
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
                  return std::views::concat(std::views::single([]<typename T>(T& route) {
                             if constexpr (std::convertible_to<T, RouteType>) {
                                 return &route;
                             } else {
                                 return nullptr;
                             }
                         }(cur_routes))...)

                         // exclude non-RouteTypes
                         | std::views::filter([](auto* route) {
                               return route != nullptr;
                           })

                         // convert to reference
                         | std::views::transform([](RouteType* route) -> RouteType& {
                               return *route;
                           });
              },
              routes);
        }

        [[nodiscard]] constexpr auto uinput_devices() noexcept {
            return routes_of<basic_uinput>();
        }

        constexpr context_action operator()(Context auto& ctx) {
            auto const& event = ctx.event();
            auto const  index = hashes.at(hash(static_cast<event_code>(event)));
            if (index < 0) [[unlikely]] {
                return context_action::ignore_event;
            }
            return visit_at(routes, index, [&](auto& route) {
                return invoke_mod(route, ctx);
            });
        }
    };

    constexpr basic_router<> router;

    static_assert(OutputModifier<basic_router<>>, "Must be an output modifier.");
} // namespace foresight
