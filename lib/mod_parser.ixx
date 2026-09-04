// Created by moisrex on 11/4/25.

module;
#include <array>
#include <cstdint>
#include <functional>
#include <linux/input-event-codes.h>
#include <span>
#include <string_view>
export module fs8.lib.mod_parser;
import fs8.event;
import fs8.lib.xkb;

namespace fs8 {

    /// A UTF-32 encoded code point that uses unused parts of Unicode
    export using code32_t = char32_t;

    export constexpr auto     invalid_code_point     = static_cast<char32_t>(0x10'FFFFU);
    export constexpr code32_t event_encoded_code32_t = 0b1U << 30U;

    /// Is this key code a modifier key (ctrl/shift/alt/meta/caps/num/scroll)?
    export [[nodiscard]] bool is_modifier_key(std::uint16_t const code) noexcept;

    using code32_callback   = std::function_ref<void(code32_t const &)>;
    using key_code_callback = std::function_ref<void(key_event const &)>;

    /// How a modifier tag is matched
    /// keydown: `<...>` — all keys at once, order doesn't matter
    /// keyup:   `[...]` — released keys, order doesn't matter
    /// ordered_keydown: `<<...>>` — keys must be pressed in the given order
    /// ordered_keyup:   `[[...]]` — keys must be released in the given order
    export enum struct [[nodiscard]] modifier_mode : std::uint8_t {
        unknown         = 0,
        keydown         = 2,
        keyup           = 3,
        ordered_keydown = 5,
        ordered_keyup   = 6,
    };

    /// Return the modifier mode a tag (with its brackets) represents.
    /// Returns `modifier_mode::unknown` for non-modifier strings.
    export template <typename CharT>
    [[nodiscard]] constexpr modifier_mode modifier_mode_of(std::basic_string_view<CharT> const tag) noexcept {
        using enum modifier_mode;
        auto const size = tag.size();
        if (size < 2) [[unlikely]] {
            return unknown;
        }
        CharT const open  = tag.front();
        CharT const close = tag.back();
        if (open == static_cast<CharT>('<') || open == static_cast<CharT>('[')) {
            bool const is_close = close == (open == static_cast<CharT>('<') ? static_cast<CharT>('>') : static_cast<CharT>(']'));
            if (!is_close) [[unlikely]] {
                return unknown;
            }
            bool const ordered = size >= 4 && tag[1] == open && tag[size - 2] == close;
            switch (open) {
                case static_cast<CharT>('<'): return ordered ? ordered_keydown : keydown;
                case static_cast<CharT>('['): return ordered ? ordered_keyup : keyup;
                default: break;
            }
        }
        return unknown;
    }

    /// Find the next modifier tag at or after `pos`, setting `begin`/`end` to the tag
    /// boundaries (brackets included). Returns false if no more tags are found.
    export template <typename CharT>
    [[nodiscard]] bool
    find_modifier_tag(std::basic_string_view<CharT> const str, std::size_t const pos, std::size_t &begin, std::size_t &end) noexcept {
        std::array<CharT, 2> const opens{static_cast<CharT>('<'), static_cast<CharT>('[')};
        auto                       pos_ = pos;
        for (;;) {
            auto const b = str.find_first_of(std::basic_string_view<CharT>{opens.data(), opens.size()}, pos_);
            if (b == std::basic_string_view<CharT>::npos) [[likely]] {
                return false;
            }
            CharT const                open    = str[b];
            CharT const                close   = open == static_cast<CharT>('<') ? static_cast<CharT>('>') : static_cast<CharT>(']');
            bool const                 ordered = b + 1 < str.size() && str[b + 1] == open;
            std::array<CharT, 2> const close2{close, close};
            auto const c = ordered ? str.find(std::basic_string_view<CharT>{close2.data(), 2}, b + 2) : str.find(close, b + 1);
            if (c == std::basic_string_view<CharT>::npos) [[unlikely]] {
                // Not a valid tag; skip the opening character as a literal.
                pos_ = b + 1;
                continue;
            }
            begin = b;
            end   = c + (ordered ? 2 : 1);
            return true;
        }
    }

    /// Convert an event into encoded code point
    export [[nodiscard]] code32_t unicode_encoded_event(xkb::basic_state const &state, key_event) noexcept;

    /// Convert to UTF-32
    export [[nodiscard]] char32_t utf8_next_code_point(std::string_view &src) noexcept;

    /// Parse UTF-8 or U+XXXX code points
    export [[nodiscard]] char32_t parse_char_or_codepoint(std::string_view &src) noexcept;

    /// Parse one or more key tags (e.g. "<f1>", "[F1][Alt]", "<alt-f1>") or a bare key
    /// name/set (e.g. "f1", "ctrl-r"), writing the resolved key codes into `out`.
    /// Returns the number of codes written.
    export [[nodiscard]] std::size_t parse_key_tags(std::string_view str, std::span<event_type::code_type> out) noexcept;

    /// Find the specified delimiter, but also checks if it's escaped or not.
    export [[nodiscard]] std::size_t find_delim(std::string_view str, char delim, std::size_t pos = 0) noexcept;
    export [[nodiscard]] std::size_t find_delim(std::u32string_view str, char32_t delim, std::size_t pos = 0) noexcept;
    export [[nodiscard]] std::size_t find_delim(std::string_view str, std::string_view delims, std::size_t pos = 0) noexcept;
    export [[nodiscard]] std::size_t find_delim(std::u32string_view str, std::u32string_view delims, std::size_t pos = 0) noexcept;

    /// Parse a modifier tag (e.g. `<ctrl-r>`, `[x]`, `<<ctrl-r>>`), call the callback on each key event.
    /// Presses the keys down and releases them (in reverse order).
    export [[nodiscard]] bool parse_modifier(std::u32string_view mod_str, key_code_callback callback);
    export [[nodiscard]] bool parse_modifier(std::u32string_view mod_str, code32_callback callback);
    export [[nodiscard]] bool parse_modifier(std::u32string_view mod_str, user_event_callback callback);
    export [[nodiscard]] bool parse_modifier(std::string_view mod_str, key_code_callback callback);
    export [[nodiscard]] bool parse_modifier(std::string_view mod_str, code32_callback callback);
    export [[nodiscard]] bool parse_modifier(std::string_view mod_str, user_event_callback callback);
    export [[nodiscard]] bool parse_modifier(std::u8string_view mod_str, key_code_callback callback);
    export [[nodiscard]] bool parse_modifier(std::u8string_view mod_str, code32_callback callback);
    export [[nodiscard]] bool parse_modifier(std::u8string_view mod_str, user_event_callback callback);

    [[nodiscard]] std::u32string parse_modifier(std::string_view mod_str);
    [[nodiscard]] std::u32string parse_modifier(std::u32string_view mod_str);

    /// Next `<...>` section
    export void on_modifier_tags(std::u32string_view str, std::function_ref<void(std::u32string_view)> callback) noexcept;

    /// Normalize the modifiers, for example replace keyboard event pairs X with Unicode Code Point X.
    /// Returns true if it was already normalized, false otherwise.
    /// `mode` decides which events are kept: key-downs for keydown modes, key-ups for keyup modes.
    export bool normalize_modifiers(std::u32string &str, modifier_mode mode = modifier_mode::keydown) noexcept;

    /// Parse the string and return the next UTF-32 code point
    export void replace_modifier_strings(std::u32string &str) noexcept;

    /// Conver to UTF-32 and encode the modifier strings into it as well
    /// Throws exceptions if input is invalid.
    export [[nodiscard]] std::u32string encoded_modifiers(std::string_view);

} // namespace fs8
