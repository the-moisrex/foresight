// Created by moisrex on 8/21/26.

module;
#include <cstdint>
#include <linux/input-event-codes.h>
#include <string>
#include <string_view>
export module fs8.mods:sanitizer;
import fs8.context;
import fs8.event;
import fs8.log;
import :drop;
import :group;
import :on_fail;

export namespace fs8 {

    /// Describes what kind of problem the sanitizer detected for an event.
    enum struct [[nodiscard]] sanitizer_issue : std::uint8_t {
        none,               ///< event is clean
        adjacent_syn,       ///< duplicate SYN_REPORT (no data since last syn)
        zero_mouse_moves,   ///< mouse move events that move nowhere
        orphan_release,     ///< key release without a prior press
        orphan_repeat,      ///< key repeat without a prior press
        double_press,       ///< key press while already pressed
        late_syn,           ///< SYN_REPORT arrived after a long gap with no data
        out_of_resolution,  ///< pen ABS value outside device bounds
        big_jump,           ///< mouse movement exceeding threshold
        missing_syn,        ///< SYN_REPORT framing broken (time, count, or travel threshold)
        missing_syn_time,   ///< data events long after last SYN_REPORT
        missing_syn_count,  ///< too many data events without SYN_REPORT
        missing_syn_travel, ///< mouse travel exceeding threshold without SYN_REPORT
        orphan_abs,         ///< ABS position event without a preceding tool press
    };

    [[nodiscard]] std::string_view to_string(sanitizer_issue issue) noexcept;
    [[nodiscard]] std::string      to_string(sanitizer_issue issue, event_type const& event) noexcept;

    /// Diagnostics callback: logs the event via fs8::log.
    /// Returns `next` — the event is NOT dropped, only logged.
    /// Overloads accept an optional `sanitizer_issue` reason.
    constexpr struct [[nodiscard]] basic_log_diagnostics : basic_log {
        context_action operator()(event_type const& event) const noexcept;
        context_action operator()(event_type const& event, sanitizer_issue reason) const noexcept;
    } log_diagnostics;

    /// Drop-only callback: returns `drop_event` without any logging.
    constexpr struct [[nodiscard]] basic_drop {
        context_action operator()(event_type const&) const noexcept {
            return context_action::drop_event;
        }
    } drop;

    /// Drop-and-log callback: logs the event and returns `drop_event`.
    constexpr struct [[nodiscard]] basic_log_and_drop : basic_log {
        context_action operator()(event_type const& event) const noexcept;
        context_action operator()(event_type const& event, sanitizer_issue reason) const noexcept;
    } log_and_drop;

    /// All default sanitizer checks with silent drop (no logging).
    constexpr auto sanitizer =
      group_mod[on_fail[drop_adjacent_syns, drop],
                on_fail[drop_zero_mouse_moves, drop],
                on_fail[drop_late_syn, drop],
                on_fail[drop_orphan_abs, drop],
                on_fail[drop_missing_syns, drop],
                on_fail[drop_big_jumps, drop]];

    /// All default sanitizer checks with diagnostics logging only (no drop).
    constexpr auto diagnostics =
      group_mod[on_fail[drop_adjacent_syns, log_diagnostics, sanitizer_issue::adjacent_syn],
                on_fail[drop_zero_mouse_moves, log_diagnostics, sanitizer_issue::zero_mouse_moves],
                on_fail[drop_late_syn, log_diagnostics, sanitizer_issue::late_syn],
                on_fail[drop_orphan_abs, log_diagnostics, sanitizer_issue::orphan_abs],
                on_fail[drop_missing_syns, log_diagnostics, sanitizer_issue::missing_syn],
                on_fail[drop_big_jumps, log_diagnostics, sanitizer_issue::big_jump]];

    /// All default sanitizer checks: drop bad events and log each one.
    constexpr auto sieve =
      group_mod[on_fail[drop_adjacent_syns, log_and_drop, sanitizer_issue::adjacent_syn],
                on_fail[drop_zero_mouse_moves, log_and_drop, sanitizer_issue::zero_mouse_moves],
                on_fail[drop_late_syn, log_and_drop, sanitizer_issue::late_syn],
                on_fail[drop_orphan_abs, log_and_drop, sanitizer_issue::orphan_abs],
                on_fail[drop_missing_syns, log_and_drop, sanitizer_issue::missing_syn],
                on_fail[drop_big_jumps, log_and_drop, sanitizer_issue::big_jump]];

} // namespace fs8
