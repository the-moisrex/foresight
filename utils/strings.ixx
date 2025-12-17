// Created by moisrex on 12/12/25.

module;
#include <ranges>
#include <string_view>
export module foresight.utils.strings;

export namespace fs8 {

    /**
     * Convert `string_view`s to `string`s
     */
    template <typename CharT = char, typename CharTraits = std::char_traits<CharT>>
    [[nodiscard]] constexpr std::basic_string<CharT, CharTraits> operator+(std::basic_string_view<CharT, CharTraits> const str) {
        return {str.data(), str.size()};
    }

    template <std::size_t N>
    struct constexpr_string {
        char data[N]{};

        consteval constexpr_string(char const (&str)[N]) noexcept {
            std::copy_n(str, N, data);
        }

        consteval bool operator==(constexpr_string<N> const str) const noexcept {
            return std::equal(str.data, str.data + N, data);
        }

        template <std::size_t N2>
        consteval bool operator==(constexpr_string<N2> const) const noexcept {
            return false;
        }

        template <std::size_t N2>
        consteval constexpr_string<N + N2 - 1> operator+(constexpr_string<N2> const str) const noexcept {
            char newchar[N + N2 - 1]{};
            std::copy_n(data, N - 1, newchar);
            std::copy_n(str.data, N2, newchar + N - 1);
            return newchar;
        }

        consteval char operator[](std::size_t n) const noexcept {
            return data[n];
        }

        [[nodiscard]] consteval std::size_t size() const noexcept {
            return N - 1;
        }

        [[nodiscard]] consteval std::string_view view() const noexcept {
            return std::string_view{data, N};
        }
    };

    template <std::size_t s1, std::size_t s2>
    consteval auto operator+(constexpr_string<s1> fs, char const (&str)[s2]) noexcept {
        return fs + constexpr_string<s2>(str);
    }

    template <std::size_t s1, std::size_t s2>
    consteval auto operator+(char const (&str)[s2], constexpr_string<s1> fs) noexcept {
        return constexpr_string<s2>(str) + fs;
    }

    template <std::size_t s1, std::size_t s2>
    consteval auto operator==(constexpr_string<s1> fs, char const (&str)[s2]) noexcept {
        return fs == constexpr_string<s2>(str);
    }

    template <std::size_t s1, std::size_t s2>
    consteval auto operator==(char const (&str)[s2], constexpr_string<s1> fs) noexcept {
        return constexpr_string<s2>(str) == fs;
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
