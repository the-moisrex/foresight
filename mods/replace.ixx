module;
#include <array>
#include <cstdint>
#include <linux/input-event-codes.h>
export module foresight.mods.replace;
import foresight.mods.event;
import foresight.mods.context;

namespace foresight {


    /**
     * Usage: on(cond, put(event_type{...})
     */
    export template <typename EventType = event_code>
    struct [[nodiscard]] basic_put {
        using code_type = event_code::code_type;

      private:
        EventType to;

      public:
        constexpr basic_put() noexcept = default;

        constexpr explicit basic_put(EventType const inp_to) noexcept : to{inp_to} {}

        constexpr basic_put(basic_put const&) noexcept            = default;
        constexpr basic_put(basic_put&&) noexcept                 = default;
        constexpr basic_put& operator=(basic_put const&) noexcept = default;
        constexpr basic_put& operator=(basic_put&&) noexcept      = default;
        constexpr ~basic_put() noexcept                           = default;

        consteval auto operator()(event_code const& code) const noexcept {
            return basic_put<event_code>{code};
        }

        consteval auto operator()(user_event const& code) const noexcept {
            return basic_put<user_event>{code};
        }

        consteval auto operator()(event_type const& code) const noexcept {
            return basic_put<event_type>{code};
        }

        consteval auto operator()(code_type const code) const noexcept {
            return basic_put<event_code>{
              event_code{.type = EV_KEY, .code = code}
            };
        }

        void operator()(Context auto& ctx) const noexcept {
            event_type& event  = ctx.event();
            event             |= to;
        }
    };

    export constexpr basic_put<> put;

    /**
     * This doesn't change the value or timestamp, just the type and the code.
     */
    export template <std::size_t N = 0, typename EventType = event_code>
    struct [[nodiscard]] basic_replace {
        using code_type = event_code::code_type;

      private:
        event_code               from;
        std::array<EventType, N> to;

      public:
        constexpr basic_replace() noexcept = default;

        constexpr explicit basic_replace(event_code const                inp_from,
                                         std::array<EventType, N> const& inp_to) noexcept
          : from{inp_from},
            to{inp_to} {}

        consteval basic_replace(basic_replace const&) noexcept            = default;
        constexpr basic_replace(basic_replace&&) noexcept                 = default;
        consteval basic_replace& operator=(basic_replace const&) noexcept = default;
        constexpr basic_replace& operator=(basic_replace&&) noexcept      = default;
        constexpr ~basic_replace() noexcept                               = default;

        template <typename... T>
            requires(std::convertible_to<T, event_code> && ...)
        consteval auto operator()(event_code const inp_from, T... inp_to) const noexcept {
            return basic_replace<sizeof...(T)>{
              inp_from,
              std::array<event_code, sizeof...(T)>{static_cast<event_code>(inp_to)...}};
        }

        template <typename... T>
            requires(std::convertible_to<T, user_event> && ...)
        consteval auto operator()(event_code const inp_from, T... inp_to) const noexcept {
            return basic_replace<sizeof...(T), user_event>{
              inp_from,
              std::array<user_event, sizeof...(T)>{static_cast<user_event>(inp_to)...}};
        }

        template <typename... T>
            requires(std::convertible_to<T, code_type> && ...)
        consteval auto operator()(event_code const inp_from, T... inp_to) const noexcept {
            return basic_replace<sizeof...(T), event_code>{
              inp_from,
              std::array<event_code, sizeof...(T)>{event_code{EV_KEY, static_cast<code_type>(inp_to)}...}};
        }

        template <typename... T>
            requires(std::convertible_to<T, code_type> && ...)
        consteval auto operator()(code_type const inp_from, T... inp_to) const noexcept {
            return basic_replace<sizeof...(T), event_code>{
              event_code{EV_KEY, inp_from},
              std::array<event_code, sizeof...(T)>{event_code{EV_KEY, static_cast<code_type>(inp_to)}...}
            };
        }

        template <typename... T>
            requires(std::convertible_to<T, event_type> && ...)
        consteval auto operator()(code_type const inp_from, T... inp_to) const noexcept {
            return basic_replace<sizeof...(T), event_type>{
              event_code{EV_KEY, inp_from},
              std::array<event_type, sizeof...(T)>{event_code{EV_KEY, static_cast<code_type>(inp_to)}...}
            };
        }

        template <std::size_t NN>
        consteval auto operator()(code_type const                   inp_from,
                                  std::array<event_type, NN> const& inp_to) const noexcept {
            return basic_replace<NN, event_type>{
              event_code{EV_KEY, inp_from},
              inp_to
            };
        }

        template <std::size_t NN>
        consteval auto operator()(code_type const                   inp_from,
                                  std::array<user_event, NN> const& inp_to) const noexcept {
            return basic_replace<NN, user_event>{
              event_code{EV_KEY, inp_from},
              inp_to
            };
        }

        template <std::size_t NN>
        consteval auto operator()(event_code const                  inp_from,
                                  std::array<event_type, NN> const& inp_to) const noexcept {
            return basic_replace<NN, event_type>{inp_from, inp_to};
        }

        template <std::size_t NN>
        consteval auto operator()(event_code const                  inp_from,
                                  std::array<user_event, NN> const& inp_to) const noexcept {
            return basic_replace<NN, user_event>{inp_from, inp_to};
        }

        void operator()(Context auto& ctx) const noexcept {
            event_type& event = ctx.event();
            if (!event.is(from)) {
                return;
            }

            // emit the events:
            if constexpr (N > 1) {
                for (std::uint8_t index = 0; index < N - 1; ++index) {
                    std::ignore = ctx.fork_emit(event | to[index]);
                    // std::ignore = ctx.fork_emit(syn());
                }
            }

            // replace the last one:
            event = to[N - 1];
        }
    };

    export constexpr basic_replace<> replace;


} // namespace foresight
