// Created by moisrex on 10/29/25.
module;
#include <cstdint>
#include <string_view>
export module foresight.utils.hash;

namespace fs8 {

    // 32-bit FNV-1a constants
    constexpr std::uint32_t FNV1A_32_INIT  = 0x811C'9DC5U;
    constexpr std::uint32_t FNV1A_32_PRIME = 0x0100'0193U;

    // 64-bit FNV-1a constants
    [[maybe_unused]] constexpr std::uint64_t FNV1A_64_INIT  = 0xCBF2'9CE4'8422'2325ULL;
    [[maybe_unused]] constexpr std::uint64_t FNV1A_64_PRIME = 0x100'0000'01B3ULL;

    /**
     * Byte Hashing - FNV-1a
     * Case-insensitive FNV-1a hash (operates on char32_t, folds ASCII A-Z->a-z while hashing)
     * std::hash<std::uint32_string_view> is not constexpr, so we implement our own.
     * http://www.isthe.com/chongo/tech/comp/fnv/
     */
    export template <typename CharT = char32_t>
    [[nodiscard]] constexpr std::uint32_t ci_hash(std::basic_string_view<CharT> const src) noexcept {
        std::uint32_t hash = FNV1A_32_INIT;
        for (auto code_point : src) {
            if (code_point >= static_cast<CharT>('A') && code_point <= static_cast<CharT>('Z')) {
                code_point += static_cast<CharT>(U'a' - U'A');
            }
            hash ^= static_cast<std::uint32_t>(code_point);
            hash *= FNV1A_32_PRIME;
        }
        return hash;
    }

    export void fnv1a_init(std::uint32_t& hash) noexcept;
    export void fnv1a_hash(std::uint32_t& hash, char32_t uch) noexcept;

    export void fnv1a_init(std::uint64_t& hash) noexcept;
    export void fnv1a_hash(std::uint64_t& hash, char32_t uch) noexcept;
} // namespace fs8
