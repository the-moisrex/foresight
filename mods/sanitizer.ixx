// Created by moisrex on 8/21/26.

module;
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <utility>
export module fs8.mods:sanitizer;
import fs8.context;
import fs8.event;
import fs8.traits;
import fs8.utils;
import :input_manager;

export namespace fs8 {

    /// Describes what kind of problem the sanitizer detected for an event.
    enum struct [[nodiscard]] sanitizer_issue : std::uint8_t {
        none,              ///< event is clean
        adjacent_syn,      ///< duplicate SYN_REPORT (no data since last syn)
        orphan_release,    ///< key release without a prior press
        late_syn,          ///< SYN_REPORT arrived after a long gap with no data
        out_of_resolution, ///< pen ABS value outside device bounds
        big_jump,          ///< mouse movement exceeding threshold
    };

    /**
     * Sanitize input events by detecting and filtering problematic events.
     *
     * Checks:
     *  - Adjacent SYN_REPORTs (duplicate sync with no data in between)
     *  - Orphan key releases (EV_KEY release for a key that was never pressed)
     *  - Late SYN_REPORTs (sync after a long gap with no data events)
     *  - Out-of-resolution pen locations (ABS values outside device bounds)
     *  - Big mouse jumps (REL movement exceeding a pixel threshold)
     *
     * The callback receives (event, issue). For two-argument callbacks it is
     * called for every event; for one-argument callbacks (like `log`) it is
     * only called for problematic events.
     *
     * Usage:
     *   event_sanitizer                           // filter bad events, no callback
     *   event_sanitizer[log["sanitized"]]         // log problematic events
     *   event_sanitizer[my_callback, .threshold(100)]
     */
    template <typename Callback = basic_noop>
    struct [[nodiscard]] basic_event_sanitizer : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;
        using code_type  = event_type::code_type;
        using msec_type  = std::chrono::microseconds;

      private:
        // --- Config ---
        bool check_adjacent_syns   = true;
        bool check_orphan_releases = true;
        bool check_late_syns       = true;
        bool check_pen_resolution  = true;
        bool check_big_jumps       = true;
        bool log_events_good       = false;

        value_type big_jump_threshold = 50;
        msec_type  late_syn_threshold{100'000};

        // --- Mutable state ---
        bool      was_syn            = false;
        bool      any_data_since_syn = false;
        msec_type last_syn_time{0};

        static constexpr std::size_t max_tracked_keys = 64;
        code_type                    pressed_keys[max_tracked_keys]{};
        std::size_t                  num_pressed = 0;

        value_type last_mouse_x   = 0;
        value_type last_mouse_y   = 0;
        bool       has_last_mouse = false;

        bool       has_pen_bounds = false;
        value_type pen_x_min      = 0;
        value_type pen_x_max      = 0;
        value_type pen_y_min      = 0;
        value_type pen_y_max      = 0;

        [[no_unique_address]] Callback cb;

        // --- Key tracking helpers ---

        [[nodiscard]] constexpr bool is_key_pressed(code_type const code) const noexcept {
            for (std::size_t i = 0; i < num_pressed; ++i) {
                if (pressed_keys[i] == code) {
                    return true;
                }
            }
            return false;
        }

        constexpr void track_press(code_type const code) noexcept {
            if (!is_key_pressed(code) && num_pressed < max_tracked_keys) {
                pressed_keys[num_pressed++] = code;
            }
        }

        constexpr void track_release(code_type const code) noexcept {
            for (std::size_t i = 0; i < num_pressed; ++i) {
                if (pressed_keys[i] == code) {
                    pressed_keys[i] = pressed_keys[--num_pressed];
                    return;
                }
            }
        }

        // --- Individual checks ---

        [[nodiscard]] sanitizer_issue check_adjacent(event_type const& event) const noexcept {
            if (event.type() == EV_SYN && event.code() == SYN_REPORT && was_syn) {
                return sanitizer_issue::adjacent_syn;
            }
            return sanitizer_issue::none;
        }

        [[nodiscard]] sanitizer_issue check_orphan(event_type const& event) const noexcept {
            if (event.type() == EV_KEY && event.value() == 0 && !is_key_pressed(event.code())) {
                return sanitizer_issue::orphan_release;
            }
            return sanitizer_issue::none;
        }

        [[nodiscard]] sanitizer_issue check_late(event_type const& event) const noexcept {
            if (event.type() == EV_SYN && event.code() == SYN_REPORT) {
                if (!any_data_since_syn && was_syn) {
                    auto const gap = event.micro_time() - last_syn_time;
                    if (gap >= late_syn_threshold) {
                        return sanitizer_issue::late_syn;
                    }
                }
            }
            return sanitizer_issue::none;
        }

        [[nodiscard]] sanitizer_issue check_pen(event_type const& event) const noexcept {
            if (event.type() == EV_ABS) {
                switch (event.code()) {
                    case ABS_X: {
                        if (event.value() < pen_x_min || event.value() > pen_x_max) {
                            return sanitizer_issue::out_of_resolution;
                        }
                        break;
                    }
                    case ABS_Y: {
                        if (event.value() < pen_y_min || event.value() > pen_y_max) {
                            return sanitizer_issue::out_of_resolution;
                        }
                        break;
                    }
                    default: break;
                }
            }
            return sanitizer_issue::none;
        }

        [[nodiscard]] sanitizer_issue check_jump(event_type const& event) const noexcept {
            if (is_mouse_movement(event) && std::abs(event.value()) > big_jump_threshold) [[unlikely]] {
                return sanitizer_issue::big_jump;
            }
            return sanitizer_issue::none;
        }

        /// Update tracking state after all checks have run.
        constexpr void update_state(event_type const& event) noexcept {
            bool const is_syn = event.type() == EV_SYN && event.code() == SYN_REPORT;

            // Track whether any data events arrived since the last SYN.
            if (!is_syn) {
                any_data_since_syn = true;
            }

            // Track key press/release state (for orphan detection).
            if (event.type() == EV_KEY) {
                switch (event.value()) {
                    case 1: track_press(event.code()); break;
                    case 0: track_release(event.code()); break;
                    default: break; // repeats (value 2) don't change state
                }
            }

            // Track last mouse position (for big-jump detection).
            if (is_mouse_movement(event)) {
                switch (event.code()) {
                    case REL_X: last_mouse_x += event.value(); break;
                    case REL_Y: last_mouse_y += event.value(); break;
                    default: break;
                }
                has_last_mouse = true;
            }

            // Reset adjacent-syn tracking on SYN_REPORT.
            if (is_syn) {
                was_syn            = true;
                any_data_since_syn = false;
                last_syn_time      = event.micro_time();
                return;
            }

            was_syn = false;
        }

        /// Dispatch the callback based on its arity.
        constexpr void invoke_callback(event_type const& event, sanitizer_issue const issue) noexcept {
            if constexpr (std::invocable<Callback&, event_type const&, sanitizer_issue>) {
                if (issue == sanitizer_issue::none && !log_events_good) {
                    return;
                }
                cb(event, issue);
            } else if constexpr (std::invocable<Callback&, event_type const&>) {
                if (issue != sanitizer_issue::none) {
                    cb(event);
                }
            }
        }

      public:
        constexpr basic_event_sanitizer() noexcept = default;

        explicit consteval basic_event_sanitizer(Callback inp_cb) noexcept : cb{std::move(inp_cb)} {}

        // --- Config builders ---

        consteval basic_event_sanitizer threshold(value_type const t) const noexcept {
            auto copy               = *this;
            copy.big_jump_threshold = t;
            return copy;
        }

        consteval basic_event_sanitizer late_syn(msec_type const d) const noexcept {
            auto copy               = *this;
            copy.late_syn_threshold = d;
            return copy;
        }

        consteval basic_event_sanitizer only_bad() const noexcept {
            auto copy            = *this;
            copy.log_events_good = false;
            return copy;
        }

        consteval basic_event_sanitizer all_events() const noexcept {
            auto copy            = *this;
            copy.log_events_good = true;
            return copy;
        }

        consteval basic_event_sanitizer adjacent_syns(bool const v = true) const noexcept {
            auto copy                = *this;
            copy.check_adjacent_syns = v;
            return copy;
        }

        consteval basic_event_sanitizer orphan_releases(bool const v = true) const noexcept {
            auto copy                  = *this;
            copy.check_orphan_releases = v;
            return copy;
        }

        consteval basic_event_sanitizer late_syns(bool const v = true) const noexcept {
            auto copy            = *this;
            copy.check_late_syns = v;
            return copy;
        }

        consteval basic_event_sanitizer pen_resolution(bool const v = true) const noexcept {
            auto copy                 = *this;
            copy.check_pen_resolution = v;
            return copy;
        }

        consteval basic_event_sanitizer big_jumps(bool const v = true) const noexcept {
            auto copy            = *this;
            copy.check_big_jumps = v;
            return copy;
        }

        // --- Compile-time factory ---

        template <typename C>
        consteval basic_event_sanitizer operator[](C&& inp_cb) const noexcept {
            auto copy = *this;
            copy.cb   = std::forward<C>(inp_cb);
            return copy;
        }

        // --- Start: seed pen resolution from input_manager ---

        template <Context CtxT>
            requires has_mod<basic_input_manager, CtxT>
        context_action operator()(CtxT& ctx, start_tag) noexcept {
            if (check_pen_resolution) {
                for (auto const& dev : ctx.mod(input_manager).devices()) {
                    if (auto const* x = dev.abs_info(ABS_X); x != nullptr) {
                        if (auto const* y = dev.abs_info(ABS_Y); y != nullptr) {
                            pen_x_min      = x->minimum;
                            pen_x_max      = x->maximum;
                            pen_y_min      = y->minimum;
                            pen_y_max      = y->maximum;
                            has_pen_bounds = true;
                            break;
                        }
                    }
                }
            }
            return context_action::next;
        }

        // --- Main handler ---

        context_action operator()(event_type const& event) noexcept {
            using enum sanitizer_issue;

            if (check_adjacent_syns) {
                if (auto const issue = check_adjacent(event); issue != none) {
                    invoke_callback(event, issue);
                    update_state(event);
                    return context_action::ignore_event;
                }
            }

            if (check_orphan_releases) {
                if (auto const issue = check_orphan(event); issue != none) {
                    invoke_callback(event, issue);
                    update_state(event);
                    return context_action::ignore_event;
                }
            }

            if (check_late_syns) {
                if (auto const issue = check_late(event); issue != none) {
                    invoke_callback(event, issue);
                    update_state(event);
                    return context_action::ignore_event;
                }
            }

            if (check_pen_resolution && has_pen_bounds) {
                if (auto const issue = check_pen(event); issue != none) {
                    invoke_callback(event, issue);
                    update_state(event);
                    return context_action::ignore_event;
                }
            }

            if (check_big_jumps) {
                if (auto const issue = check_jump(event); issue != none) {
                    invoke_callback(event, issue);
                    update_state(event);
                    return context_action::ignore_event;
                }
            }

            invoke_callback(event, none);
            update_state(event);
            return context_action::next;
        }
    };

    constexpr basic_event_sanitizer<> event_sanitizer;

} // namespace fs8
