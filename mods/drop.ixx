// Created by moisrex on 6/25/25.

module;
#include <array>
#include <chrono>
#include <cstddef>
#include <linux/input-event-codes.h>
#include <utility>
export module fs8.mods:drop;
import fs8.context;
import fs8.devices.capabilities;
import fs8.traits;
import fs8.log;
import :debounce;
import :input_manager;

export namespace fs8 {

    constexpr struct [[nodiscard]] basic_drop_abs {
        context_action operator()(event_type const& event) const noexcept;
    } drop_abs;

    constexpr struct [[nodiscard]] basic_drop_tablet {
        context_action operator()(event_type const& event) const noexcept;
    } drop_tablet;

    /**
     * Ignore Big Mouse Jumps
     * Any jumps bigger than specified threshold is ignored
     */
    constexpr struct [[nodiscard]] basic_drop_big_jumps : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;

        static constexpr value_type default_threshold = 50;

      private:
        value_type threshold = default_threshold; // pixels to resistance to move
      public:
        constexpr explicit basic_drop_big_jumps(value_type const inp_threshold) noexcept : threshold{inp_threshold} {}

        consteval basic_drop_big_jumps operator[](value_type const inp_threshold) const noexcept {
            return basic_drop_big_jumps{inp_threshold};
        }

        context_action operator()(event_type const& event) const noexcept;
    } drop_big_jumps;

    /**
     * Ignore initial mouse moves
     */
    constexpr struct [[nodiscard]] basic_drop_init_moves : consteval_copyable {
        using consteval_copyable::consteval_copyable;

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
        constexpr explicit basic_drop_init_moves(value_type const inp_threshold,
                                                 msec_type const  inp_time_threshold = default_time_threshold) noexcept
          : threshold{inp_threshold},
            time_threshold{inp_time_threshold} {}

        consteval basic_drop_init_moves operator[](value_type const inp_threshold,
                                                   msec_type const  inp_time_threshold = default_time_threshold) const noexcept {
            return basic_drop_init_moves{inp_threshold, inp_time_threshold};
        }

        context_action operator()(event_type const& event) noexcept;
    } drop_init_moves;

    constexpr struct [[nodiscard]] basic_drop_mouse_moves {
        context_action operator()(event_type const& event) const noexcept;
    } drop_mouse_moves;

    constexpr struct [[nodiscard]] basic_drop_zero_mouse_moves {
        context_action operator()(event_type const& event) const noexcept;
    } drop_zero_mouse_moves;

    constexpr struct [[nodiscard]] basic_drop_mouse_clicks {
        context_action operator()(event_type const& event) const noexcept;
    } drop_mouse_clicks;

    constexpr struct [[nodiscard]] basic_drop_fast_repeats : consteval_copyable {
        using consteval_copyable::consteval_copyable;
        using msec_type = std::chrono::microseconds;

      private:
        static constexpr msec_type default_time_threshold = std::chrono::milliseconds(30);

        user_event code{};
        msec_type  time_threshold{default_time_threshold};
        msec_type  last_emitted{0};

      public:
        constexpr explicit basic_drop_fast_repeats(event_code const inp_code,
                                                   msec_type const  inp_time_threshold = default_time_threshold) noexcept
          : code{.type = inp_code.type, .code = inp_code.code, .value = 1},
            time_threshold{inp_time_threshold} {}

        consteval basic_drop_fast_repeats operator[](event_code const inp_code,
                                                     msec_type const  inp_time_threshold = default_time_threshold) const noexcept {
            return basic_drop_fast_repeats{inp_code, inp_time_threshold};
        }

        context_action operator()(event_type const& event) noexcept;
    } drop_fast_repeats;

    /// Deprecated aliases of the general `fs8.mods:debounce` mod.
    ///
    /// `basic_drop_fast_double_clicks` used to be a mouse-click-only debouncer;
    /// it is now a thin alias of `basic_debounce` in `click` mode. Prefer the
    /// `debounce` mod going forward.
    template <std::size_t N>
    using basic_drop_fast_double_clicks = basic_debounce<N>;

    constexpr basic_debounce<0> drop_fast_double_clicks;

    constexpr basic_debounce<1> drop_fast_left_double_clicks{
      {.type = EV_KEY, .code = BTN_LEFT},
    };

    constexpr basic_debounce<1> drop_fast_right_double_clicks{
      {.type = EV_KEY, .code = BTN_RIGHT},
    };

    constexpr basic_debounce<1> drop_fast_middle_double_clicks{
      {.type = EV_KEY, .code = BTN_MIDDLE},
    };

    template <std::size_t N>
    struct [[nodiscard]] basic_drop_keys : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::array<event_code, N> codes{};

      public:
        constexpr explicit basic_drop_keys(std::array<event_code, N> const inp_codes) noexcept : codes{inp_codes} {}

        template <std::size_t NN>
        consteval basic_drop_keys operator[](std::array<event_code, NN> const inp_codes) const noexcept {
            return basic_drop_keys<NN>{inp_codes};
        }

        template <std::size_t NN>
        consteval auto operator[](event_code (&&inp_codes)[NN]) const noexcept {
            return basic_drop_keys<NN>{std::to_array(std::move(inp_codes))};
        }

        template <typename... T>
            requires((std::convertible_to<T, event_type::code_type> && ...))
        consteval auto operator[](T... inp_codes) const noexcept {
            return basic_drop_keys<sizeof...(T)>{
              std::array<event_code, sizeof...(T)>{event_code{.type = EV_KEY, .code = static_cast<event_type::code_type>(inp_codes)}...}};
        }

        context_action operator()(event_type const& event) const noexcept {
            for (event_code const code : codes) {
                if (event.is(code)) {
                    return context_action::drop_event;
                }
            }
            return context_action::next;
        }
    };

    constexpr basic_drop_keys<0> drop_keys;

    /// Ignore the auto-repeat events (EV_KEY with value 2) of the given keys,
    /// while letting their press/release events pass through.
    template <std::size_t N>
    struct [[nodiscard]] basic_drop_repeats_of : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::array<event_type::code_type, N> codes{};

      public:
        constexpr explicit basic_drop_repeats_of(std::array<event_type::code_type, N> const inp_codes) noexcept : codes{inp_codes} {}

        template <std::size_t NN>
        consteval basic_drop_repeats_of operator[](std::array<event_type::code_type, NN> const inp_codes) const noexcept {
            return basic_drop_repeats_of<NN>{inp_codes};
        }

        template <typename... T>
            requires((std::convertible_to<T, event_type::code_type> && ...))
        consteval auto operator[](T... inp_codes) const noexcept {
            return basic_drop_repeats_of<sizeof...(T)>{
              std::array<event_type::code_type, sizeof...(T)>{static_cast<event_type::code_type>(inp_codes)...}};
        }

        context_action operator()(event_type const& event) const noexcept {
            using enum context_action;
            if (event.type() != EV_KEY || event.value() != 2) {
                return next;
            }
            for (event_type::code_type const code : codes) {
                if (event.code() == code) {
                    return drop_event;
                }
            }
            return next;
        }
    };

    constexpr basic_drop_repeats_of<0> drop_repeats_of;

    constexpr struct [[nodiscard]] basic_drop_caps : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        dev_caps_view caps{};

      public:
        constexpr explicit basic_drop_caps(dev_caps_view const inp_caps) noexcept : caps{inp_caps} {}

        consteval auto operator[](dev_caps_view const inp_caps) const noexcept {
            return basic_drop_caps{inp_caps};
        }

        context_action operator()(event_type const& event) const noexcept;
    } drop_caps;

    constexpr struct [[nodiscard]] basic_drop_start_moves : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::uint32_t emit_threshold = 50;
        std::uint32_t emitted_count{0};

      public:
        constexpr explicit basic_drop_start_moves(std::uint32_t const inp_time_threshold) noexcept : emit_threshold{inp_time_threshold} {}

        consteval basic_drop_start_moves operator[](std::uint32_t const inp_time_threshold) const noexcept {
            return basic_drop_start_moves{inp_time_threshold};
        }

        void           operator()(special_event const& tag) noexcept;
        context_action operator()(event_type const& event) noexcept;
    } drop_start_moves;

    constexpr struct [[nodiscard]] basic_drop_adjacent_repeats : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        event_code asked_event{};
        bool       is_found = false;

      public:
        constexpr explicit basic_drop_adjacent_repeats(event_code const code) noexcept : asked_event{code} {}

        constexpr explicit basic_drop_adjacent_repeats(event_type const code) noexcept : asked_event{static_cast<event_code>(code)} {}

        consteval basic_drop_adjacent_repeats operator[](event_code const code) const noexcept {
            return basic_drop_adjacent_repeats{code};
        }

        context_action operator()(event_type const& event) noexcept;
    } drop_adjacent_repeats;

    constexpr struct [[nodiscard]] basic_exit_pipeline {
        constexpr context_action operator()(event_type const&) const noexcept {
            log("Exit requested.");
            return context_action::exit;
        }
    } exit_pipeline;

    constexpr struct [[nodiscard]] basic_drop_event {
        constexpr context_action operator()(event_type const&) const noexcept {
            return context_action::drop_event;
        }
    } drop_event;

    constexpr struct [[nodiscard]] basic_drop_msc_scan {
        constexpr context_action operator()(event_type const& event) const noexcept;
    } drop_msc_scan;

    constexpr context_action basic_drop_msc_scan::operator()(event_type const& event) const noexcept {
        using enum context_action;
        return event.is(EV_MSC, MSC_SCAN) ? drop_event : next;
    }

    constexpr basic_drop_adjacent_repeats drop_adjacent_syns{syn()};

    constexpr basic_drop_fast_repeats drop_fast_left_clicks{
      {.type = EV_KEY, .code = BTN_LEFT},
    };

    constexpr basic_drop_fast_repeats drop_fast_right_clicks{
      {.type = EV_KEY, .code = BTN_RIGHT},
    };

    // todo: ignore_types(EV_ABS)
    // todo: ignore_codes(EV_BTN_TOOL_RUBBER)

    /// Enforce valid key event state transitions.
    ///
    /// Maintains a per-key pressed flag.  The following transitions are
    /// considered valid and pass through:
    ///   - not pressed → press (value 1)
    ///   - pressed → repeat (value 2)
    ///   - pressed → release (value 0)
    ///
    /// Everything else is filtered:
    ///   - not pressed → repeat (orphan repeat, e.g. key held at startup)
    ///   - not pressed → release (orphan release)
    ///   - pressed → press (double press / bounce)
    ///
    /// Invalid events never update state, so the tracker stays consistent.
    constexpr struct [[nodiscard]] basic_enforce_key_state : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using code_type = event_type::code_type;

      private:
        static constexpr std::size_t max_keys = 256;
        std::array<bool, max_keys>   pressed{};

      public:
        constexpr void operator()(special_event const& tag) noexcept {
            if (tag.code == start.code) {
                pressed = {};
            }
        }

        context_action operator()(event_type const& event) noexcept;

        [[nodiscard]] constexpr bool is_key_pressed(code_type const code) const noexcept {
            return pressed[static_cast<std::size_t>(code)];
        }
    } enforce_key_state;

    /// Ignore late SYN_REPORTs — empty sync packets that arrive after a long
    /// idle gap with no data events in between.
    constexpr struct [[nodiscard]] basic_drop_late_syn : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using msec_type = std::chrono::microseconds;

      private:
        static constexpr msec_type default_threshold{100'000}; // 100 ms

        msec_type threshold{default_threshold};
        // state
        bool      was_syn            = false;
        bool      any_data_since_syn = false;
        msec_type last_syn_time{0};

      public:
        constexpr explicit basic_drop_late_syn(msec_type const inp_threshold = default_threshold) noexcept : threshold{inp_threshold} {}

        consteval basic_drop_late_syn operator[](msec_type const inp_threshold) const noexcept {
            return basic_drop_late_syn{inp_threshold};
        }

        context_action operator()(event_type const& event) noexcept;
    } drop_late_syn;

    /// Ignore pen ABS values that fall outside the device-reported bounds.
    ///
    /// Pen bounds are seeded from `input_manager` on `special_event`.  Only
    /// `ABS_X` and `ABS_Y` events are checked.
    constexpr struct [[nodiscard]] basic_drop_pen_out_of_bounds : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;

      private:
        bool       has_pen_bounds = false;
        value_type pen_x_min      = 0;
        value_type pen_x_max      = 0;
        value_type pen_y_min      = 0;
        value_type pen_y_max      = 0;

      public:
        constexpr void
        seed_pen_bounds(value_type const x_min, value_type const x_max, value_type const y_min, value_type const y_max) noexcept {
            pen_x_min      = x_min;
            pen_x_max      = x_max;
            pen_y_min      = y_min;
            pen_y_max      = y_max;
            has_pen_bounds = true;
        }

        template <typename CtxT>
            requires has_mod<basic_input_manager, CtxT>
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
            if (tag.code != start.code) {
                return context_action::drop_event;
            }
            for (auto const& dev : ctx.mod(input_manager).devices()) {
                if (auto const* x = dev.abs_info(ABS_X); x != nullptr) {
                    if (auto const* y = dev.abs_info(ABS_Y); y != nullptr) {
                        seed_pen_bounds(x->minimum, x->maximum, y->minimum, y->maximum);
                        return context_action::next;
                    }
                }
            }
            return context_action::next;
        }

        context_action operator()(event_type const& event) const noexcept;
    } drop_pen_out_of_bounds;

    /// Ignore ABS_X/ABS_Y position events arriving before any BTN_TOOL_* has
    /// been pressed.  A tool press (value 1) on any BTN_TOOL_* arms position
    /// tracking; a tool release (value 0) disarms it.
    constexpr struct [[nodiscard]] basic_drop_orphan_abs : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        bool tool_active = false;

      public:
        constexpr void operator()(special_event const& tag) noexcept {
            if (tag.code == start.code) {
                tool_active = false;
            }
        }

        context_action operator()(event_type const& event) noexcept;
    } drop_orphan_abs;

    /// Ignore data events when the SYN_REPORT framing is broken.
    ///
    /// Tracks three conditions:
    ///  - `missing_syn_time`: a data event arrives long after the last SYN
    ///  - `missing_syn_count`: too many data events without a SYN
    ///  - `missing_syn_travel`: cumulative mouse travel exceeds threshold without a SYN
    constexpr struct [[nodiscard]] basic_drop_missing_syns : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;
        using msec_type  = std::chrono::microseconds;

      private:
        msec_type  time_threshold{100'000}; // 100 ms
        value_type count_threshold  = 20;
        value_type travel_threshold = 500;
        // state
        msec_type  last_syn_time{0};
        value_type data_events_since_syn = 0;
        value_type travel_since_syn      = 0;

      public:
        constexpr explicit basic_drop_missing_syns(
          msec_type const  inp_time_threshold   = msec_type{100'000},
          value_type const inp_count_threshold  = 20,
          value_type const inp_travel_threshold = 500) noexcept
          : time_threshold{inp_time_threshold},
            count_threshold{inp_count_threshold},
            travel_threshold{inp_travel_threshold} {}

        consteval basic_drop_missing_syns time(msec_type const d) const noexcept {
            auto copy           = *this;
            copy.time_threshold = d;
            return copy;
        }

        consteval basic_drop_missing_syns count(value_type const n) const noexcept {
            auto copy            = *this;
            copy.count_threshold = n;
            return copy;
        }

        consteval basic_drop_missing_syns travel(value_type const d) const noexcept {
            auto copy             = *this;
            copy.travel_threshold = d;
            return copy;
        }

        context_action operator()(event_type const& event) noexcept;
    } drop_missing_syns;

} // namespace fs8
