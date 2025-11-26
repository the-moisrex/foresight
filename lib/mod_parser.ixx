// Created by moisrex on 11/4/25.

module;
#include <cstdint>
#include <functional>
#include <string_view>
export module foresight.lib.mod_parser;
import foresight.mods.event;
import foresight.lib.xkb;

namespace fs8 {

    /// A UTF-32 encoded code point that uses unused parts of Unicode
    export using code32_t = char32_t;

    export constexpr auto     invalid_code_point     = static_cast<char32_t>(0x10'FFFFU);
    export constexpr code32_t event_encoded_code32_t = 0b1U << 30U;

    using code32_callback = std::function<void(code32_t const &)> const &; // todo: use std::function_ref
    using key_code_callback =
      std::function<void(key_event_code const &)> const &;                 // todo: use std::function_ref

    /// Convert an event into encoded code point
    export [[nodiscard]] code32_t unicode_encoded_event(xkb::basic_state const &state,
                                                        event_type const &) noexcept;

    /// Convert to UTF-32
    export [[nodiscard]] char32_t utf8_next_code_point(std::string_view &src) noexcept;

    /// Parse UTF-8 or U+XXXX code points
    export [[nodiscard]] char32_t parse_char_or_codepoint(std::string_view &src) noexcept;

    /// Find the specified delimiter, but also checks if it's escaped or not.
    export std::size_t find_delim(std::string_view str, char delim, std::size_t pos = 0) noexcept;
    export std::size_t find_delim(std::u32string_view str, char32_t delim, std::size_t pos = 0) noexcept;

    /// Parse a string that starts with `<` and ends with `>`, call the callback function on them.
    export [[nodiscard]] bool parse_modifier(std::u32string_view mod_str, key_code_callback callback);
    export [[nodiscard]] bool parse_modifier(std::u32string_view mod_str, code32_callback callback);
    export [[nodiscard]] bool parse_modifier(std::u32string_view mod_str, user_event_callback callback);
    export [[nodiscard]] bool parse_modifier(std::string_view mod_str, key_code_callback callback);
    export [[nodiscard]] bool parse_modifier(std::string_view mod_str, code32_callback callback);
    export [[nodiscard]] bool parse_modifier(std::string_view mod_str, user_event_callback callback);

    [[nodiscard]] std::u32string parse_modifier(std::string_view mod_str);
    [[nodiscard]] std::u32string parse_modifier(std::u32string_view mod_str);

    /// Next `<...>` section
    export void on_modifier_tags(std::u32string_view                             str,
                                 std::function<void(std::u32string_view)> const &callback) noexcept;

    /// Parse the string and return the next UTF-32 code point
    export void replace_modifiers_and_actions(std::u32string &str) noexcept;
} // namespace fs8
