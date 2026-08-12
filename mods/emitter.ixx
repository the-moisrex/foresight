// Created by moisrex on 6/22/25.

module;
#include <algorithm>
#include <array>
#include <linux/input-event-codes.h>
#include <span>
export module fs8.mods.emitter;
import fs8.event;
import fs8.context;
import fs8.traits;

namespace fs8 {

    export template <std::size_t N>
    struct [[nodiscard]] basic_emit : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::array<user_event, N> events{};

      public:
        explicit consteval basic_emit(std::array<user_event, N> const inp) noexcept : events{inp} {}

        template <std::size_t NN>
        consteval auto operator+(std::array<user_event, NN> const& new_events) const noexcept {
            std::array<user_event, N + NN> result;
            std::copy_n(events.begin(), N, result.begin());
            std::copy_n(new_events.begin(), NN, std::next(result.begin(), N));
            return basic_emit<N + NN>{result};
        }

        consteval auto operator+(user_event const& event) const noexcept {
            return operator+(std::array{event});
        }

        template <std::size_t NN>
        consteval auto operator[](std::array<user_event, NN> const& new_events) const noexcept {
            return operator+(new_events);
        }

        // NOLINTBEGIN(*-avoid-c-arrays)
        template <std::size_t NN>
        consteval auto operator[](user_event (&&new_events)[NN]) const noexcept {
            return operator[](std::to_array(std::move(new_events)));
        }

        template <std::size_t NN>
        consteval auto operator+(user_event (&&new_events)[NN]) const noexcept {
            return operator[](std::to_array(std::move(new_events)));
        }

        // NOLINTEND(*-avoid-c-arrays)

        consteval auto operator[](user_event const& event) const noexcept {
            return operator+(std::array{event});
        }

        void operator()(Context auto& ctx) noexcept {
            for (auto const& usr_event : events) {
                std::ignore = ctx.fork_emit(event_type{usr_event});
            }
        }
    };

    export constexpr basic_emit<0> emit;

    export constexpr struct [[nodiscard]] basic_scheduled_emitter : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::span<user_event const> events;

      public:
        void schedule(std::span<user_event const> const new_events) noexcept {
            events = new_events;
        }

        void operator()(auto&&, Tag auto) = delete;
        void operator()(Tag auto)         = delete;

        context_action operator()(event_type& event, next_event_tag) noexcept;
    } scheduled_emitter;

    export template <std::size_t N>
    struct [[nodiscard]] basic_schedule_emit : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::array<user_event, N> events;

      public:
        explicit consteval basic_schedule_emit(std::array<user_event, N> const inp) noexcept : events{inp} {}

        template <std::size_t NN>
        consteval auto operator+(std::array<user_event, NN> const& new_events) const noexcept {
            std::array<user_event, N + NN> result;
            std::copy_n(events.begin(), N, result.begin());
            std::copy_n(new_events.begin(), NN, std::next(result.begin(), N));
            return basic_schedule_emit<N + NN>{result};
        }

        consteval auto operator+(user_event const& event) const noexcept {
            return operator+(std::array{event});
        }

        template <std::size_t NN>
        consteval auto operator[](std::array<user_event, NN> const& new_events) const noexcept {
            return operator+(new_events);
        }

        consteval auto operator[](user_event const& event) const noexcept {
            return operator+(std::array{event});
        }

        void operator()(Context auto& ctx) const noexcept {
            ctx.mod(scheduled_emitter).schedule(events);
        }
    };

    export basic_schedule_emit<0> schedule_emit;

    export [[nodiscard]] constexpr std::array<user_event, 2> up(event_type::code_type const code) noexcept {
        return std::array{
          user_event{.type = EV_KEY, .code = code, .value = 0},
          syn_user_event
        };
    }

    export [[nodiscard]] constexpr std::array<user_event, 2> down(event_type::code_type const code) noexcept {
        return std::array{
          user_event{.type = EV_KEY, .code = code, .value = 1},
          syn_user_event
        };
    }

    export [[nodiscard]] constexpr std::array<user_event, 4> keypress(event_type::code_type const code) noexcept {
        return std::array{
          user_event{.type = EV_KEY, .code = code, .value = 1},
          syn_user_event,
          user_event{.type = EV_KEY, .code = code, .value = 0},
          syn_user_event,
        };
    }

    export [[nodiscard]] constexpr std::array<user_event, 2> turn_led_on(event_type::code_type const code) noexcept {
        return std::array{
          user_event{.type = EV_LED, .code = code, .value = 1},
          syn_user_event
        };
    }

    export [[nodiscard]] constexpr std::array<user_event, 2> turn_led_off(event_type::code_type const code) noexcept {
        return std::array{
          user_event{.type = EV_LED, .code = code, .value = 0},
          syn_user_event
        };
    }

    export template <typename... CT>
    [[nodiscard]] constexpr auto press(CT const... codes) noexcept {
        std::array<user_event, (sizeof...(CT) * 2) + 2> events;
        auto                                            pos = events.begin();
        // NOLINTBEGIN(*-use-designated-initializers)
        ((*pos++ = user_event{EV_KEY, static_cast<event_type::code_type>(codes), 1}), ...);
        *pos++ = syn_user_event;
        ((*pos++ = user_event{EV_KEY, static_cast<event_type::code_type>(codes), 0}), ...);
        // NOLINTEND(*-use-designated-initializers)
        *pos = syn_user_event;
        return events;
    }

    export constexpr struct [[nodiscard]] basic_replace_code : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using ev_type   = event_type::type_type;
        using code_type = event_type::code_type;

      private:
        ev_type   find_type = EV_MAX;
        code_type find_code = KEY_MAX;

        ev_type   rep_type = EV_MAX;
        code_type rep_code = KEY_MAX;

      public:
        constexpr basic_replace_code(
          ev_type const   inp_find_type,
          code_type const inp_find_code,
          ev_type const   inp_rep_type,
          code_type const inp_rep_code) noexcept
          : find_type{inp_find_type},
            find_code{inp_find_code},
            rep_type{inp_rep_type},
            rep_code{inp_rep_code} {}

        consteval basic_replace_code operator[](
          ev_type const   inp_find_type,
          code_type const inp_find_code,
          ev_type const   inp_rep_type,
          code_type const inp_rep_code) const noexcept {
            return basic_replace_code{inp_find_type, inp_find_code, inp_rep_type, inp_rep_code};
        }

        void operator()(event_type& event) const noexcept;
    } replace_code;

    // todo: implement replace_all which used table lookup

    export template <std::size_t N = 0>
    struct [[nodiscard]] basic_emit_all : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::array<user_event, N> events{};
        std::size_t               index = 0;

      public:
        explicit constexpr basic_emit_all(std::array<user_event, N> const inp_events) noexcept : events{inp_events} {}

        template <std::size_t NN>
        consteval auto operator[](std::array<user_event, NN> const new_events) const noexcept {
            return basic_emit_all<NN>{new_events};
        }

        // NOLINTBEGIN(*-avoid-c-arrays)
        template <std::size_t NN>
        consteval auto operator[](user_event (&&new_events)[NN]) const noexcept {
            return basic_emit_all<NN>{std::to_array(std::move(new_events))};
        }

        // NOLINTEND(*-avoid-c-arrays)

        // template <typename... T>
        //     requires(sizeof...(T) > 1)
        // consteval auto operator[](T... new_events) const noexcept {
        //     return basic_emit_all<sizeof...(T)>{std::array<user_event, sizeof...(T)>{new_events...}};
        // }

        template <Context CtxT>
        context_action operator()(CtxT& ctx, load_event_tag) noexcept {
            using enum context_action;
            if (index == N) [[unlikely]] {
                return exit;
            }
            auto& event = ctx.event();
            event       = events.at(index);
            event.reset_time();
            ++index;
            return next;
        }
    };

    export constexpr basic_emit_all<> emit_all;


} // namespace fs8
