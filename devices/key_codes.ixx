// Created by moisrex on 10/24/25.
module;
#include <cstdint>
#include <string_view>
export module foresight.devices.key_codes;

namespace fs8 {

    /**
     * Get the event code of the specified key name.
     */
    export template <typename CharT>
    [[nodiscard]] std::uint16_t key_code_of(std::basic_string_view<CharT> name) noexcept;

    template <>
    [[nodiscard]] std::uint16_t key_code_of<char32_t>(std::basic_string_view<char32_t> name) noexcept;
    template <>
    [[nodiscard]] std::uint16_t key_code_of<char16_t>(std::basic_string_view<char16_t> name) noexcept;
    template <>
    [[nodiscard]] std::uint16_t key_code_of<char8_t>(std::basic_string_view<char8_t> name) noexcept;
    template <>
    [[nodiscard]] std::uint16_t key_code_of<char>(std::basic_string_view<char> name) noexcept;
} // namespace fs8
