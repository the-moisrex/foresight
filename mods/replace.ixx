module;
#include <array>
#include <cstdint>
#include <linux/input-event-codes.h>
export module fs8.mods:replace;
import fs8.event;
import fs8.context;
import fs8.traits;

namespace fs8 {


    /**
     * Usage: on[cond, put[event_type{...}]
     */
    export template <typename EventType = event_code>
    struct [[nodiscard]] basic_put : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using code_type = event_code::code_type;

      private:
        EventType to;

      public:
        constexpr explicit basic_put(EventType const inp_to) noexcept : to{inp_to} {}

        consteval auto operator[](event_code const& code) const noexcept {
            return basic_put<event_code>{code};
        }

        consteval auto operator[](user_event const& code) const noexcept {
            return basic_put<user_event>{code};
        }

        consteval auto operator[](event_type const& code) const noexcept {
            return basic_put<event_type>{code};
        }

        consteval auto operator[](code_type const code) const noexcept {
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
    struct [[nodiscard]] basic_replace : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using code_type = event_code::code_type;

      private:
        event_code               from{};
        std::array<EventType, N> to{};

      public:
        constexpr explicit basic_replace(event_code const inp_from, std::array<EventType, N> const& inp_to) noexcept
          : from{inp_from},
            to{inp_to} {}

        template <typename... T>
            requires(std::convertible_to<T, event_code> && ...)
        consteval auto operator[](event_code const inp_from, T... inp_to) const noexcept {
            return basic_replace<sizeof...(T)>{inp_from, std::array<event_code, sizeof...(T)>{static_cast<event_code>(inp_to)...}};
        }

        template <typename... T>
            requires(std::convertible_to<T, event_code> && ...)
        consteval auto operator[](code_type const inp_code, T... inp_to) const noexcept {
            return basic_replace<sizeof...(T)>{key_code(inp_code),
                                               std::array<event_code, sizeof...(T)>{static_cast<event_code>(inp_to)...}};
        }

        template <typename... T>
            requires(std::convertible_to<T, user_event> && ...)
        consteval auto operator[](event_code const inp_from, T... inp_to) const noexcept {
            return basic_replace<sizeof...(T), user_event>{inp_from,
                                                           std::array<user_event, sizeof...(T)>{static_cast<user_event>(inp_to)...}};
        }

        template <typename... T>
            requires(std::convertible_to<T, code_type> && ...)
        consteval auto operator[](event_code const inp_from, T... inp_to) const noexcept {
            return basic_replace<sizeof...(T), event_code>{inp_from, key_codes(inp_to...)};
        }

        template <typename... T>
            requires(std::convertible_to<T, code_type> && ...)
        consteval auto operator[](code_type const inp_from, T... inp_to) const noexcept {
            return basic_replace<sizeof...(T), event_code>{
              event_code{EV_KEY, inp_from},
              key_codes(inp_to...)
            };
        }

        template <typename... T>
            requires(std::convertible_to<T, event_type> && ...)
        consteval auto operator[](code_type const inp_from, T... inp_to) const noexcept {
            return basic_replace<sizeof...(T), event_type>{
              event_code{EV_KEY, inp_from},
              std::array<event_type, sizeof...(T)>{static_cast<event_type>(inp_to)...}
            };
        }

        template <std::size_t NN>
        consteval auto operator[](code_type const inp_from, std::array<event_type, NN> const& inp_to) const noexcept {
            return basic_replace<NN, event_type>{
              event_code{EV_KEY, inp_from},
              inp_to
            };
        }

        template <std::size_t NN>
        consteval auto operator[](code_type const inp_from, std::array<user_event, NN> const& inp_to) const noexcept {
            return basic_replace<NN, user_event>{
              event_code{EV_KEY, inp_from},
              inp_to
            };
        }

        template <std::size_t NN>
        consteval auto operator[](event_code const inp_from, std::array<event_type, NN> const& inp_to) const noexcept {
            return basic_replace<NN, event_type>{inp_from, inp_to};
        }

        template <std::size_t NN>
        consteval auto operator[](event_code const inp_from, std::array<user_event, NN> const& inp_to) const noexcept {
            return basic_replace<NN, user_event>{inp_from, inp_to};
        }

        void operator()(Context auto& ctx) const noexcept {
            event_type& event = ctx.event();
            if (!event.is(from)) {
                return;
            }

            // emit the events:
            if constexpr (N > 1) {
                if (event.value() == 0) {
                    for (std::uint8_t index = N - 1; index > 0; --index) {
                        std::ignore = ctx.fork_emit(event | to[index]);
                        std::ignore = ctx.fork_emit(syn());
                    }

                    // replace the last one:
                    event |= to[0];
                } else {
                    for (std::uint8_t index = 0; index < N - 1; ++index) {
                        std::ignore = ctx.fork_emit(event | to[index]);
                        std::ignore = ctx.fork_emit(syn());
                    }

                    // replace the last one:
                    event |= to[N - 1];
                }
            }
        }
    };

    export constexpr basic_replace<> replace;


} // namespace fs8
