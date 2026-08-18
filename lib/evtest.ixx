// Created by moisrex on 8/18/26.

module;
#include <string_view>
export module fs8.lib.evtest;
import fs8.event;

export namespace fs8 {

    /// One parsed line of evtest-format output.
    struct [[nodiscard]] parsed_evtest_event {
        /// The parsed type/code/value.
        user_event event;
        /// The "time" field of the line, in seconds.
        double time = 0;
    };

    /// Parse a single line of evtest (or `foresight how-to-type --evtest`)
    /// output into a `parsed_evtest_event`.
    ///
    /// Handles lines like:
    ///   Event: time 1634220472.123456, type 1 (EV_KEY), code 29 (KEY_LEFTCTRL), value 1
    ///
    /// Returns false for lines that are not event lines: SYN_REPORT separators,
    /// evtest's header/preamble lines, empty lines, and malformed input.
    /// The timestamp is optional; when missing it defaults to 0.
    [[nodiscard]] bool parse_evtest_line(std::string_view line, parsed_evtest_event& out) noexcept;

} // namespace fs8
