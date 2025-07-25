// Created by moisrex on 6/22/25.

module;
#include <algorithm>
#include <array>
#include <linux/input-event-codes.h>
export module foresight.mods.emitter;
import foresight.mods.event;
import foresight.mods.context;

namespace foresight {


    export template <std::size_t N>
    struct [[nodiscard]] emitter {
      private:
        std::array<user_event, N> events;

      public:
        explicit consteval emitter(std::array<user_event, N> const inp) noexcept : events{inp} {}

        constexpr emitter() noexcept                     = default;
        constexpr emitter(emitter&&)                     = default;
        constexpr emitter(emitter const&) noexcept       = default;
        constexpr emitter& operator=(emitter const&)     = default;
        constexpr emitter& operator=(emitter&&) noexcept = default;
        constexpr ~emitter()                             = default;

        template <std::size_t NN>
        consteval auto operator+(std::array<user_event, NN> const& new_events) const noexcept {
            std::array<user_event, N + NN> result;
            std::copy_n(events.begin(), N, result.begin());
            std::copy_n(new_events.begin(), NN, std::next(result.begin(), N));
            return emitter<N + NN>{result};
        }

        consteval auto operator+(user_event const& event) const noexcept {
            return operator+(std::array{event});
        }

        template <std::size_t NN>
        consteval auto operator()(std::array<user_event, NN> const& new_events) const noexcept {
            return operator+(new_events);
        }

        consteval auto operator()(user_event const& event) const noexcept {
            return operator+(std::array{event});
        }

        template <Context CtxT>
        constexpr void operator()(CtxT& ctx) noexcept {
            for (auto const& usr_event : events) {
                std::ignore = ctx.fork_emit(event_type{usr_event});
            }
        }
    };

    export constexpr emitter<0> emit;

    export [[nodiscard]] constexpr std::array<user_event, 2> up(event_type::code_type const code) noexcept {
        return std::array{
          user_event{EV_KEY, code, 0},
          static_cast<user_event>(syn())
        };
    }

    export [[nodiscard]] constexpr std::array<user_event, 2> down(event_type::code_type const code) noexcept {
        return std::array{
          user_event{EV_KEY, code, 1},
          static_cast<user_event>(syn())
        };
    }

    export [[nodiscard]] constexpr std::array<user_event, 4> keypress(
      event_type::code_type const code) noexcept {
        return std::array{
          user_event{EV_KEY, code, 1},
          static_cast<user_event>(syn()),
          user_event{EV_KEY, code, 0},
          static_cast<user_event>(syn()),
        };
    }

    export [[nodiscard]] constexpr std::array<user_event, 2> turn_led_on(
      event_type::code_type const code) noexcept {
        return std::array{
            user_event{EV_LED, code, 1},
            static_cast<user_event>(syn())
          };
    }

    export [[nodiscard]] constexpr std::array<user_event, 2> turn_led_off(
      event_type::code_type const code) noexcept {
        return std::array{
            user_event{EV_LED, code, 0},
            static_cast<user_event>(syn())
          };
    }

    export template <typename... CT>
    [[nodiscard]] constexpr auto press(CT const... codes) noexcept {
        std::array<user_event, sizeof...(CT) * 2 + 2> events;
        auto                                          pos = events.begin();
        ((*pos++ = user_event{.type = EV_KEY, .code = static_cast<event_type::code_type>(codes), .value = 1}),
         ...);
        *pos++ = static_cast<user_event>(syn());
        ((*pos++ = user_event{.type = EV_KEY, .code = static_cast<event_type::code_type>(codes), .value = 0}),
         ...);
        *pos = static_cast<user_event>(syn());
        return events;
    }

    constexpr struct [[nodiscard]] basic_replace {
        using ev_type   = event_type::type_type;
        using code_type = event_type::code_type;

      private:
        ev_type   find_type = EV_MAX;
        code_type find_code = KEY_MAX;

        ev_type   rep_type = EV_MAX;
        code_type rep_code = KEY_MAX;

      public:
        constexpr basic_replace(
          ev_type const   inp_find_type,
          code_type const inp_find_code,
          ev_type const   inp_rep_type,
          code_type const inp_rep_code) noexcept
          : find_type{inp_find_type},
            find_code{inp_find_code},
            rep_type{inp_rep_type},
            rep_code{inp_rep_code} {}

        constexpr basic_replace() noexcept                                = default;
        consteval basic_replace(basic_replace const&) noexcept            = default;
        consteval basic_replace& operator=(basic_replace const&) noexcept = default;
        constexpr basic_replace& operator=(basic_replace&&) noexcept      = default;
        constexpr ~basic_replace() noexcept                               = default;

        consteval basic_replace operator()(
          ev_type const   inp_find_type,
          code_type const inp_find_code,
          ev_type const   inp_rep_type,
          code_type const inp_rep_code) const noexcept {
            return basic_replace{inp_find_type, inp_find_code, inp_rep_type, inp_rep_code};
        }

        constexpr void operator()(event_type& event) const noexcept;
    } replace;

    // todo: implement replace_all which used table lookup

} // namespace foresight
