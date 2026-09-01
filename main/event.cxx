// Created by moisrex on 8/17/26.

module;
#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>
module fs8.event;

[[nodiscard]] std::string_view fs8::to_source_string(std::uint32_t const source_id) noexcept {
    if (source_id == source_id_none) {
        return {"none"};
    }

    static thread_local std::array<char, 32> buf{};
    auto const                               first = buf.data();

    // Format: "mod:XXXX,idx:XXXX"
    auto const mid = std::copy(std::begin("mod:"), std::end("mod:") - 1, first);
    auto [ptr, ec] = std::to_chars(mid, buf.data() + buf.size() - 1, sid(source_id), 16);
    if (ec != std::errc{}) [[unlikely]] {
        return {"<unknown>"};
    }
    *ptr++               = ',';
    ptr                  = std::copy(std::begin("idx:"), std::end("idx:") - 1, ptr);
    auto const remaining = static_cast<std::size_t>(buf.data() + buf.size() - ptr);
    if (remaining < 6) [[unlikely]] {
        return {"<unknown>"};
    }
    auto const [ptr2, ec2] = std::to_chars(ptr, buf.data() + buf.size() - 1, source_index(source_id), 16);
    if (ec2 != std::errc{}) [[unlikely]] {
        return {"<unknown>"};
    }
    return std::string_view{first, static_cast<std::size_t>(ptr2 - first)};
}
