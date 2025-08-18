// Created by moisrex on 6/22/25.

module;
#include <algorithm>
#include <array>
#include <linux/input-event-codes.h>
#include <span>
export module foresight.mods.emitter;
import foresight.mods.event;
import foresight.mods.context;

namespace foresight {


    export template <std::size_t N>
    struct [[nodiscard]] basic_emit {
      private:
        std::array<user_event, N> events{};

      public:
        explicit consteval basic_emit(std::array<user_event, N> const inp) noexcept : events{inp} {}

        constexpr basic_emit() noexcept                        = default;
        constexpr basic_emit(basic_emit&&)                     = default;
        constexpr basic_emit(basic_emit const&) noexcept       = default;
        constexpr basic_emit& operator=(basic_emit const&)     = default;
        constexpr basic_emit& operator=(basic_emit&&) noexcept = default;
        constexpr ~basic_emit()                                = default;

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
        consteval auto operator()(std::array<user_event, NN> const& new_events) const noexcept {
            return operator+(new_events);
        }

        template <std::size_t NN>
        consteval auto operator()(user_event (&&new_events)[NN]) const noexcept {
            return operator()(std::to_array(std::move(new_events)));
        }

        template <std::size_t NN>
        consteval auto operator+(user_event (&&new_events)[NN]) const noexcept {
            return operator()(std::to_array(std::move(new_events)));
        }

        consteval auto operator()(user_event const& event) const noexcept {
            return operator+(std::array{event});
        }

        void operator()(Context auto& ctx) noexcept {
            for (auto const& usr_event : events) {
                std::ignore = ctx.fork_emit(event_type{usr_event});
            }
        }
    };

    export constexpr basic_emit<0> emit;

    export constexpr struct [[nodiscard]] basic_scheduled_emitter {
      private:
        std::span<user_event const> events;

      public:
        constexpr basic_scheduled_emitter() noexcept                                     = default;
        constexpr basic_scheduled_emitter(basic_scheduled_emitter&&)                     = default;
        constexpr basic_scheduled_emitter(basic_scheduled_emitter const&) noexcept       = default;
        constexpr basic_scheduled_emitter& operator=(basic_scheduled_emitter const&)     = default;
        constexpr basic_scheduled_emitter& operator=(basic_scheduled_emitter&&) noexcept = default;
        constexpr ~basic_scheduled_emitter()                                             = default;

        void schedule(std::span<user_event const> const new_events) noexcept {
            events = new_events;
        }

        void operator()(Context auto& ctx) noexcept {
            for (auto const& usr_event : events) {
                std::ignore = ctx.fork_emit(event_type{usr_event});
            }
            events = {};
        }
    } scheduled_emitter;

    export template <std::size_t N>
    struct [[nodiscard]] basic_schedule_emit {
      private:
        std::array<user_event, N> events;

      public:
        explicit consteval basic_schedule_emit(std::array<user_event, N> const inp) noexcept : events{inp} {}

        constexpr basic_schedule_emit() noexcept                                 = default;
        constexpr basic_schedule_emit(basic_schedule_emit&&)                     = default;
        constexpr basic_schedule_emit(basic_schedule_emit const&) noexcept       = default;
        constexpr basic_schedule_emit& operator=(basic_schedule_emit const&)     = default;
        constexpr basic_schedule_emit& operator=(basic_schedule_emit&&) noexcept = default;
        constexpr ~basic_schedule_emit()                                         = default;

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
        consteval auto operator()(std::array<user_event, NN> const& new_events) const noexcept {
            return operator+(new_events);
        }

        consteval auto operator()(user_event const& event) const noexcept {
            return operator+(std::array{event});
        }

        void operator()(Context auto& ctx) const noexcept {
            ctx.mod(scheduled_emitter).schedule(events);
        }
    };

    export basic_schedule_emit<0> schedule_emit;

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

    export template <std::size_t N = 0>
    struct [[nodiscard]] basic_emit_all {
      private:
        std::array<user_event, N> events;

      public:
        explicit constexpr basic_emit_all(std::array<user_event, N> const inp_events) noexcept
          : events{inp_events} {}

        constexpr basic_emit_all() noexcept                            = default;
        constexpr basic_emit_all(basic_emit_all&&)                     = default;
        constexpr basic_emit_all(basic_emit_all const&) noexcept       = default;
        constexpr basic_emit_all& operator=(basic_emit_all const&)     = default;
        constexpr basic_emit_all& operator=(basic_emit_all&&) noexcept = default;
        constexpr ~basic_emit_all()                                    = default;

        template <std::size_t NN>
        consteval auto operator()(std::array<user_event, NN> const new_events) const noexcept {
            return basic_emit_all<NN>{new_events};
        }

        template <std::size_t NN>
        consteval auto operator()(user_event (&&new_events)[NN]) const noexcept {
            return basic_emit_all<NN>{std::to_array(std::move(new_events))};
        }

        template <typename... T>
            requires(sizeof...(T) > 1)
        consteval auto operator()(T... new_events) const noexcept {
            return basic_emit_all<sizeof...(T)>{{new_events...}};
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;
            for (auto const& usr_event : events) {
                std::ignore = ctx.fork_emit(event_type{usr_event});
            }
            return exit;
        }
    };

    export constexpr basic_emit_all<> emit_all;


} // namespace foresight
