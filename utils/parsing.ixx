// Created by moisrex on 9/3/26.

module;
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
export module fs8.parsing;
import fs8.event;

export namespace fs8 {

    /// Parse a duration string like "50", "50ms", "1s", "500us" into microseconds.
    [[nodiscard]] constexpr std::optional<std::chrono::microseconds> parse_duration(std::string_view const str) noexcept {
        std::uint64_t value  = 0;
        auto const [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
        if (ec != std::errc{} || ptr == str.data()) {
            return std::nullopt;
        }

        std::string_view const suffix = str.substr(static_cast<std::size_t>(ptr - str.data()));
        if (suffix == "s") {
            return std::chrono::seconds(value);
        }
        if (suffix == "us" || suffix == "\xC2\xB5s") {
            return std::chrono::microseconds(value);
        }
        if (suffix.empty() || suffix == "ms") {
            return std::chrono::milliseconds(value);
        }
        return std::nullopt;
    }

    /// Parse an event code from a name like "BTN_LEFT" (EV_KEY implied) or
    /// "EV_ABS:ABS_X". Returns nullopt for unknown names.
    [[nodiscard]] std::optional<event_code> parse_code(std::string_view str) noexcept;

    /// Parse a comma-separated list of event codes.
    template <std::size_t MaxN = 16>
    [[nodiscard]] std::optional<std::array<event_code, MaxN>> parse_codes(std::string_view str) noexcept {
        std::array<event_code, MaxN> out{};
        std::size_t                  count = 0;
        while (!str.empty()) {
            auto const                      comma = str.find(',');
            auto const                      token = str.substr(0, comma);
            std::optional<event_code> const code  = parse_code(token);
            if (!code.has_value()) {
                return std::nullopt;
            }
            if (count < out.size()) {
                out[count++] = *code;
            }
            if (comma == std::string_view::npos) {
                break;
            }
            str = str.substr(comma + 1);
        }
        return count == 0 ? std::nullopt : std::optional{out};
    }

} // namespace fs8
