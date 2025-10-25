// Created by moisrex on 10/24/25.

module;
#include <cstdint>
#include <string_view>
module foresight.devices.key_codes;
import foresight.devices.event_codes;

using foresight::keynames_type;

namespace {
    [[nodiscard]] constexpr char to_lower(char const ch8) noexcept {
        constexpr char A    = 'A';
        constexpr char Z    = 'Z';
        constexpr char diff = 'a' - A;
        if (ch8 >= A && ch8 <= Z) {
            return static_cast<char>(ch8 + diff);
        }
        return ch8;
    }

    struct case_insensitive_less_t {
        template <typename CharT, typename Traits = std::char_traits<CharT>>
        [[nodiscard]] constexpr bool operator()(keynames_type const&                  lhs,
                                                std::basic_string_view<CharT, Traits> rhs) const noexcept {
            auto len = std::min(lhs.name.size(), rhs.size());
            for (std::size_t i = 0; i < len; ++i) {
                auto const ca = static_cast<unsigned char>(lhs.name[i]);
                auto const cb = static_cast<unsigned char>(to_lower(rhs[i]));
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
        template <typename CharT, typename Traits = std::char_traits<CharT>>
        [[nodiscard]] constexpr bool operator()(keynames_type const&                  lhs,
                                                std::basic_string_view<CharT, Traits> rhs) const noexcept {
            if (lhs.name.size() != rhs.size()) {
                return false;
            }
            for (std::size_t i = 0; i < lhs.name.size(); ++i) {
                auto const ca = static_cast<unsigned char>(lhs.name[i]);
                auto const cb = static_cast<unsigned char>(to_lower(rhs[i]));
                if (ca != cb) {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace

template <typename CharT>
[[nodiscard]] std::uint16_t foresight::key_code_of(std::basic_string_view<CharT> name) noexcept {
    auto it = std::lower_bound(keynames.begin(), keynames.end(), name, case_insensitive_less_t{});
    if (it != keynames.end() && case_insensitive_equal_t{}(*it, name)) {
        return it->value;
    }
    return 0;
}
