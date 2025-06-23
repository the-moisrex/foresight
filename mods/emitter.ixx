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
                ctx.fork_emit(event_type{usr_event});
            }
        }
    };

    export constexpr emitter<0> emit;

    export constexpr auto keyup(event_type::code_type const code) noexcept {
        return std::array{
          user_event{EV_KEY, code, 0},
          static_cast<user_event>(syn())
        };
    }

    export constexpr auto keydown(event_type::code_type const code) noexcept {
        return std::array{
          user_event{EV_KEY, code, 1},
          static_cast<user_event>(syn())
        };
    }

    export constexpr auto keypress(event_type::code_type const code) noexcept {
        return std::array{
          user_event{EV_KEY, code, 1},
          static_cast<user_event>(syn()),
          user_event{EV_KEY, code, 0},
          static_cast<user_event>(syn()),
        };
    }

    export template <typename... CT>
    constexpr auto press(CT const... codes) noexcept {
        std::array<user_event, sizeof...(CT) * 2 + 1> events;
        auto                                          pos = events.begin();
        ((*pos++ = user_event{EV_KEY, static_cast<event_type::code_type>(codes), 1}, 0), ...);
        ((*pos++ = user_event{EV_KEY, static_cast<event_type::code_type>(codes), 0}, 0), ...);
        *pos = static_cast<user_event>(syn());
        return events;
    }

} // namespace foresight
