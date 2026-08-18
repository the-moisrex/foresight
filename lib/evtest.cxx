// Created by moisrex on 8/18/26.

module;
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>
module fs8.lib.evtest;
import fs8.event;

using fs8::parsed_evtest_event;
using fs8::user_event;

namespace {
    constexpr std::string_view event_prefix = "Event: time ";

    [[nodiscard]] std::string_view trim(std::string_view const str) noexcept {
        auto first = 0;
        auto last  = static_cast<std::ptrdiff_t>(str.size());
        while (first < last && (str.at(first) == ' ' || str.at(first) == '\t')) {
            ++first;
        }
        while (
          last > first && (str.at(last - 1) == ' ' || str.at(last - 1) == '\t' || str.at(last - 1) == '\r' || str.at(last - 1) == '\n'))
        {
            --last;
        }
        return str.substr(first, static_cast<std::size_t>(last - first));
    }

    /// Expect the next comma-separated field to start with `label`, then parse
    /// its leading integer. Skips spaces, the comma, and any trailing
    /// annotation (e.g. " (EV_KEY)") after the number.
    template <std::integral T>
    [[nodiscard]] std::optional<T> parse_field(std::string_view& str, std::string_view const label) noexcept {
        while (!str.empty() && str.front() == ' ') {
            str.remove_prefix(1);
        }
        if (str.empty() || str.front() != ',') {
            return std::nullopt;
        }
        str.remove_prefix(1);
        while (!str.empty() && str.front() == ' ') {
            str.remove_prefix(1);
        }
        if (!str.starts_with(label)) {
            return std::nullopt;
        }
        str.remove_prefix(label.size());

        T value              = 0;
        auto const [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
        if (ec != std::errc{} || ptr == str.data()) {
            return std::nullopt;
        }
        str.remove_prefix(static_cast<std::size_t>(ptr - str.data()));

        // Skip the rest of the field, e.g. " (KEY_LEFTCTRL)".
        while (!str.empty() && str.front() != ',') {
            str.remove_prefix(1);
        }
        return value;
    }
} // namespace

bool fs8::parse_evtest_line(std::string_view const line, parsed_evtest_event& out) noexcept {
    std::string_view str = trim(line);
    if (!str.starts_with(event_prefix)) [[unlikely]] {
        return false;
    }
    str.remove_prefix(event_prefix.size());

    // Timestamp up to the first comma; the SYN_REPORT separator lines
    // ("Event: time 0.000000, -------------- SYN_REPORT ------------") have no
    // type/code/value fields, so they fail the field parsing below.
    auto const comma = str.find(',');
    if (comma != std::string_view::npos) [[likely]] {
        double time          = 0;
        auto const [ptr, ec] = std::from_chars(str.data(), str.data() + comma, time, std::chars_format::fixed);
        if (ec != std::errc{} || ptr == str.data()) [[unlikely]] {
            return false;
        }
        out.time = time;
        str.remove_prefix(comma);
    } else {
        out.time = 0;
    }

    auto const type  = parse_field<std::uint16_t>(str, "type ");
    auto const code  = parse_field<std::uint16_t>(str, "code ");
    auto const value = parse_field<std::int32_t>(str, "value ");
    if (!type || !code || !value) [[unlikely]] {
        return false;
    }

    out.event = user_event{.type = *type, .code = *code, .value = *value};
    return true;
}
