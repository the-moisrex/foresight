// Created by moisrex on 12/12/25.

module;
#include <ranges>
#include <string_view>
export module fs8.strings;

export namespace fs8 {

    /**
     * Convert `string_view`s to `string`s
     */
    template <typename CharT = char, typename CharTraits = std::char_traits<CharT>>
    [[nodiscard]] constexpr std::basic_string<CharT, CharTraits> operator+(std::basic_string_view<CharT, CharTraits> const str) {
        return {str.data(), str.size()};
    }

    /**
     * Check if a Unicode code point is a surrogate.
     * Those code points are used only in UTF-16 encodings.
     */
    [[nodiscard]] bool is_surrogate(char32_t const cp) noexcept {
        return cp >= U'\xd800' && cp <= U'\xDFFF';
    }

    [[nodiscard]] bool is_empty(char const *src) noexcept {
        return src == nullptr || *src == '\0';
    }

    // ASCII-only case-insensitive equality (keeps your original semantics)
    template <typename CharT, typename CharT2>
    [[nodiscard]] bool iequals(std::basic_string_view<CharT> const lhs, std::basic_string_view<CharT2> const rhs) noexcept {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        static constexpr auto to_lower = []<typename CC>(CC const ch32) constexpr noexcept -> CC {
            if (ch32 >= static_cast<CC>('A') && ch32 <= static_cast<CC>('Z')) {
                return ch32 + static_cast<CC>(U'a' - U'A');
            }
            return ch32;
        };

        return std::ranges::equal(lhs, rhs, {}, to_lower, to_lower);
    }
} // namespace fs8
