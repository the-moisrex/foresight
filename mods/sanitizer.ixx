// Created by moisrex on 8/21/26.

module;
#include <linux/input-event-codes.h>
export module fs8.mods:sanitizer;
import fs8.context;
import fs8.event;
import fs8.log;
import :drop;
import :group;
import :on_fail;

export namespace fs8 {

    /// Diagnostics callback: logs the event via fs8::log.
    /// Returns `next` — the event is NOT dropped, only logged.
    constexpr struct [[nodiscard]] basic_log_diagnostics : basic_log {
        context_action operator()(event_type const& event) const noexcept;
    } log_diagnostics;

    /// Drop-only callback: returns `drop_event` without any logging.
    constexpr struct [[nodiscard]] basic_drop_action {
        context_action operator()(event_type const&) const noexcept {
            return context_action::drop_event;
        }
    } drop_action;

    /// Drop-and-log callback: logs the event and returns `drop_event`.
    constexpr struct [[nodiscard]] basic_log_and_drop_action : basic_log {
        context_action operator()(event_type const& event) const noexcept;
    } log_and_drop_action;

    /// All default sanitizer checks with silent drop (no logging).
    constexpr auto event_sanitizer =
      group_mod[on_fail[drop_adjacent_syns, drop_action],
                on_fail[drop_late_syn, drop_action],
                on_fail[drop_orphan_abs, drop_action],
                on_fail[drop_missing_syns, drop_action],
                on_fail[drop_big_jumps, drop_action]];

    /// All default sanitizer checks with diagnostics logging only (no drop).
    constexpr auto event_diagnostics =
      group_mod[on_fail[drop_adjacent_syns, log_diagnostics],
                on_fail[drop_late_syn, log_diagnostics],
                on_fail[drop_orphan_abs, log_diagnostics],
                on_fail[drop_missing_syns, log_diagnostics],
                on_fail[drop_big_jumps, log_diagnostics]];

    /// All default sanitizer checks: drop bad events and log each one.
    constexpr auto event_sieve =
      group_mod[on_fail[drop_adjacent_syns, log_and_drop_action],
                on_fail[drop_late_syn, log_and_drop_action],
                on_fail[drop_orphan_abs, log_and_drop_action],
                on_fail[drop_missing_syns, log_and_drop_action],
                on_fail[drop_big_jumps, log_and_drop_action]];

} // namespace fs8
