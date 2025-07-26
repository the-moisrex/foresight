// Created by moisrex on 6/25/25.

module;
#include <chrono>
#include <linux/input-event-codes.h>
export module foresight.mods.ignore;
import foresight.mods.context;

export namespace foresight {

    constexpr struct [[nodiscard]] basic_ignore_abs {
        context_action operator()(event_type const& event) const noexcept;
    } ignore_abs;

    constexpr struct [[nodiscard]] basic_ignore_big_jumps {
        using value_type = event_type::value_type;

      private:
        value_type threshold = 20; // pixels to resistance to move
      public:
        constexpr basic_ignore_big_jumps() noexcept = default;

        constexpr explicit basic_ignore_big_jumps(value_type const inp_threshold) noexcept
          : threshold{inp_threshold} {}

        consteval basic_ignore_big_jumps(basic_ignore_big_jumps const&) noexcept            = default;
        constexpr basic_ignore_big_jumps(basic_ignore_big_jumps&&) noexcept                 = default;
        consteval basic_ignore_big_jumps& operator=(basic_ignore_big_jumps const&) noexcept = default;
        constexpr basic_ignore_big_jumps& operator=(basic_ignore_big_jumps&&) noexcept      = default;
        constexpr ~basic_ignore_big_jumps() noexcept                                        = default;

        consteval basic_ignore_big_jumps operator()(value_type const inp_threshold) const noexcept {
            return basic_ignore_big_jumps{inp_threshold};
        }

        context_action operator()(event_type const& event) const noexcept;
    } ignore_big_jumps;

    constexpr struct [[nodiscard]] basic_ignore_init_moves {
        using value_type = event_type::value_type;
        using msec_type  = std::chrono::microseconds;

      private:
        static constexpr msec_type default_time_threshold = std::chrono::milliseconds(1000);
        // user defined thresholds:
        value_type threshold                              = 7; // pixels to resistance to move
        msec_type  time_threshold{default_time_threshold};

        // active values:
        value_type init_distance    = 0;
        bool       is_left_btn_down = false;
        msec_type  last_moved{0};

      public:
        constexpr basic_ignore_init_moves() noexcept = default;

        constexpr explicit basic_ignore_init_moves(
          value_type const inp_threshold,
          msec_type const  inp_time_threshold = default_time_threshold) noexcept
          : threshold{inp_threshold},
            time_threshold{inp_time_threshold} {}

        consteval basic_ignore_init_moves(basic_ignore_init_moves const&) noexcept            = default;
        constexpr basic_ignore_init_moves(basic_ignore_init_moves&&) noexcept                 = default;
        consteval basic_ignore_init_moves& operator=(basic_ignore_init_moves const&) noexcept = default;
        constexpr basic_ignore_init_moves& operator=(basic_ignore_init_moves&&) noexcept      = default;
        constexpr ~basic_ignore_init_moves() noexcept                                         = default;

        consteval basic_ignore_init_moves operator()(
          value_type const inp_threshold,
          msec_type const  inp_time_threshold = default_time_threshold) const noexcept {
            return basic_ignore_init_moves{inp_threshold, inp_time_threshold};
        }

        context_action operator()(event_type const& event) noexcept;
    } ignore_init_moves;

    constexpr struct [[nodiscard]] basic_ignore_mouse_moves {
        context_action operator()(event_type const& event) noexcept;
    } ignore_mouse_moves;

    constexpr struct [[nodiscard]] basic_ignore_fast_repeats {
        using msec_type = std::chrono::microseconds;

      private:
        static constexpr msec_type default_time_threshold = std::chrono::milliseconds(30);

        user_event code{};
        msec_type  time_threshold{default_time_threshold};
        msec_type  last_emitted{0};

      public:
        constexpr basic_ignore_fast_repeats() noexcept = default;

        constexpr explicit basic_ignore_fast_repeats(
          event_code const inp_code,
          msec_type const  inp_time_threshold = default_time_threshold) noexcept
          : code{.type = inp_code.type, .code = inp_code.code, .value = 1},
            time_threshold{inp_time_threshold} {}

        consteval basic_ignore_fast_repeats(basic_ignore_fast_repeats const&) noexcept            = default;
        constexpr basic_ignore_fast_repeats(basic_ignore_fast_repeats&&) noexcept                 = default;
        consteval basic_ignore_fast_repeats& operator=(basic_ignore_fast_repeats const&) noexcept = default;
        constexpr basic_ignore_fast_repeats& operator=(basic_ignore_fast_repeats&&) noexcept      = default;
        constexpr ~basic_ignore_fast_repeats() noexcept                                           = default;

        consteval basic_ignore_fast_repeats operator()(
          event_code const inp_code,
          msec_type const  inp_time_threshold = default_time_threshold) const noexcept {
            return basic_ignore_fast_repeats{inp_code, inp_time_threshold};
        }

        context_action operator()(event_type const& event) noexcept;
    } ignore_fast_repeats;

    template <std::size_t N>
    struct [[nodiscard]] basic_ignore_keys {
      private:
        std::array<event_code, N> codes{};

      public:
        constexpr basic_ignore_keys() noexcept = default;

        constexpr explicit basic_ignore_keys(std::array<event_code, N> const inp_codes) noexcept
          : codes{inp_codes} {}

        consteval basic_ignore_keys(basic_ignore_keys const&) noexcept            = default;
        constexpr basic_ignore_keys(basic_ignore_keys&&) noexcept                 = default;
        consteval basic_ignore_keys& operator=(basic_ignore_keys const&) noexcept = default;
        constexpr basic_ignore_keys& operator=(basic_ignore_keys&&) noexcept      = default;
        constexpr ~basic_ignore_keys() noexcept                                   = default;

        consteval basic_ignore_keys operator()(std::array<event_code, N> const inp_codes) const noexcept {
            return basic_ignore_keys{inp_codes};
        }

        template <typename... T>
            requires((std::convertible_to<T, event_code> && ...))
        consteval auto operator()(T... inp_codes) const noexcept {
            return basic_ignore_keys<sizeof...(T)>{
              std::array<event_code, sizeof...(T)>{static_cast<event_code>(inp_codes)...}};
        }

        template <typename... T>
            requires((std::convertible_to<T, event_type::code_type> && ...))
        consteval auto operator()(T... inp_codes) const noexcept {
            return basic_ignore_keys<sizeof...(T)>{std::array<event_code, sizeof...(T)>{
              event_code{.type = EV_KEY, .code = static_cast<event_type::code_type>(inp_codes)}...}};
        }

        context_action operator()(event_type const& event) const noexcept {
            for (event_code const code : codes) {
                if (event.is(code)) {
                    return context_action::ignore_event;
                }
            }
            return context_action::next;
        }
    };

    constexpr basic_ignore_keys<0> ignore_keys;

    constexpr struct [[nodiscard]] basic_ignore_start_moves {
        using msec_type = std::chrono::microseconds;

      private:
        static constexpr msec_type default_rest_time = std::chrono::milliseconds(100);

        std::uint32_t emit_threshold = 50;
        std::uint32_t emitted_count{0};
        msec_type     rest_time{default_rest_time};
        msec_type     last_emitted{0};

      public:
        constexpr basic_ignore_start_moves() noexcept = default;

        constexpr explicit basic_ignore_start_moves(
          std::uint32_t const inp_time_threshold,
          msec_type const     inp_rest_time = default_rest_time) noexcept
          : emit_threshold{inp_time_threshold},
            rest_time{inp_rest_time} {}

        consteval basic_ignore_start_moves(basic_ignore_start_moves const&) noexcept            = default;
        constexpr basic_ignore_start_moves(basic_ignore_start_moves&&) noexcept                 = default;
        consteval basic_ignore_start_moves& operator=(basic_ignore_start_moves const&) noexcept = default;
        constexpr basic_ignore_start_moves& operator=(basic_ignore_start_moves&&) noexcept      = default;
        constexpr ~basic_ignore_start_moves() noexcept                                          = default;

        consteval basic_ignore_start_moves operator()(
          std::uint32_t const inp_time_threshold,
          msec_type const     inp_rest_time = default_rest_time) const noexcept {
            return basic_ignore_start_moves{inp_time_threshold, inp_rest_time};
        }

        context_action operator()(event_type const& event) noexcept;
    } ignore_start_moves;

    constexpr struct [[nodiscard]] basic_ignore_adjacent_repeats {
      private:
        event_code asked_event{};
        bool       is_found = false;

      public:
        constexpr basic_ignore_adjacent_repeats() noexcept = default;

        constexpr explicit basic_ignore_adjacent_repeats(event_code const code) noexcept
          : asked_event{code} {}

        constexpr explicit basic_ignore_adjacent_repeats(event_type const code) noexcept
          : asked_event{static_cast<event_code>(code)} {}

        consteval basic_ignore_adjacent_repeats(basic_ignore_adjacent_repeats const&) noexcept = default;
        constexpr basic_ignore_adjacent_repeats(basic_ignore_adjacent_repeats&&) noexcept      = default;
        consteval basic_ignore_adjacent_repeats& operator=(
          basic_ignore_adjacent_repeats const&) noexcept = default;
        constexpr basic_ignore_adjacent_repeats& operator=(
          basic_ignore_adjacent_repeats&&) noexcept         = default;
        constexpr ~basic_ignore_adjacent_repeats() noexcept = default;

        consteval basic_ignore_adjacent_repeats operator()(event_code const code) const noexcept {
            return basic_ignore_adjacent_repeats{code};
        }

        context_action operator()(event_type const& event) noexcept;
    } ignore_adjacent_repeats;

    constexpr basic_ignore_adjacent_repeats ignore_adjacent_syns{syn()};

    constexpr basic_ignore_fast_repeats ignore_fast_left_clicks{
      {EV_KEY, BTN_LEFT}
    };

    constexpr basic_ignore_fast_repeats ignore_fast_right_clicks{
      {EV_KEY, BTN_RIGHT}
    };

    // todo: ignore_types(EV_ABS)
    // todo: ignore_codes(EV_BTN_TOOL_RUBBER)

} // namespace foresight
