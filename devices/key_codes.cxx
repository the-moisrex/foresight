// Created by moisrex on 10/24/25.

module;
#include <algorithm>
#include <cstdint>
#include <string_view>
module foresight.devices.key_codes;
import foresight.devices.event_codes;

using fs8::keynames_type;

namespace {
    template <typename CharT>
    [[nodiscard]] constexpr CharT to_lower(CharT const ch8) noexcept {
        constexpr CharT A    = 'A';
        constexpr CharT Z    = 'Z';
        constexpr CharT diff = 'a' - A;
        if (ch8 >= A && ch8 <= Z) {
            return static_cast<CharT>(ch8 + diff);
        }
        return ch8;
    }

    struct case_insensitive_less_t {
        template <typename CharT>
        [[nodiscard]] constexpr bool operator()(keynames_type const&          lhs,
                                                std::basic_string_view<CharT> rhs) const noexcept {
            auto len = std::min(lhs.name.size(), rhs.size());
            for (std::size_t i = 0; i < len; ++i) {
                auto const ca = static_cast<CharT>(lhs.name[i]);
                auto const cb = to_lower(rhs[i]);
                if (ca < cb) {
                    return true;
                }
                if (ca > cb) {
                    return false;
                }
            }
            return lhs.name.size() < rhs.size();
        }
    };

    struct case_insensitive_equal_t {
        template <typename CharT>
        [[nodiscard]] constexpr bool operator()(keynames_type const&          lhs,
                                                std::basic_string_view<CharT> rhs) const noexcept {
            if (lhs.name.size() != rhs.size()) {
                return false;
            }
            for (std::size_t i = 0; i < lhs.name.size(); ++i) {
                if (static_cast<CharT>(lhs.name[i]) != to_lower(rhs[i])) {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace

namespace {
    template <typename CharT>
    [[nodiscard]] std::uint16_t key_code_of_impl(std::basic_string_view<CharT> name) noexcept {
        using fs8::keynames;
        auto it = std::lower_bound(keynames.begin(), keynames.end(), name, case_insensitive_less_t{});
        if (it != keynames.end() && case_insensitive_equal_t{}(*it, name)) {
            return it->value;
        }
        return 0;
    }
} // namespace

template <>
std::uint16_t fs8::key_code_of<char32_t>(std::basic_string_view<char32_t> const name) noexcept {
    return key_code_of_impl(name);
}

template <>
std::uint16_t fs8::key_code_of<char16_t>(std::basic_string_view<char16_t> const name) noexcept {
    return key_code_of_impl(name);
}

template <>
std::uint16_t fs8::key_code_of<char8_t>(std::basic_string_view<char8_t> const name) noexcept {
    return key_code_of_impl(name);
}

template <>
std::uint16_t fs8::key_code_of<char>(std::basic_string_view<char> const name) noexcept {
    return key_code_of_impl(name);
}
