// Created by moisrex on 11/4/25.

module;
#include <cstdint>
#include <string_view>
export module foresight.lib.mod_parser;
import foresight.mods.event;

namespace foresight {

    /// A UTF-32 encoded code point that uses unused parts of Unicode
    export using code32_t = char32_t;

    export constexpr char32_t invalid_code_point = UINT32_MAX;

    /// Convert to UTF-32
    export [[nodiscard]] char32_t utf8_next_code_point(std::string_view &src, std::size_t max_size) noexcept;

    /// Parse UTF-8 or U+XXXX code points
    export [[nodiscard]] char32_t parse_char_or_codepoint(std::string_view &src) noexcept;

    /// Find the specified delimiter, but also checks if it's escaped or not.
    export std::size_t find_delim(std::u32string_view str, char32_t delim, std::size_t pos = 0) noexcept;

    /// Parse a string that starts with `<` and ends with `>`, call the callback function on them.
    export [[nodiscard]] bool parse_modifier(std::u32string_view mod_str, user_event_callback callback);

    /// Parse the string and return the next UTF-32 code point
    export [[nodiscard]] code32_t parse_next_code_point(std::u32string_view &str) noexcept;
} // namespace foresight
