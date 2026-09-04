// Created by moisrex on 9/4/26.

module;
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <unistd.h>
module fs8.mods;

using fs8::event_type;

// ── capture_binary_format ────────────────────────────────────────────────────

bool fs8::capture_binary_format::write_header(int const fd) const noexcept {
    struct __attribute__((packed)) {
        std::uint32_t magic;
        std::uint16_t version;
    } constexpr hdr{magic, version};
    auto const result = ::write(fd, &hdr, sizeof(hdr));
    return result == sizeof(hdr);
}

bool fs8::capture_binary_format::emit(int const fd, std::span<event_type const> const evs) const noexcept {
    if (evs.empty()) {
        return true;
    }
    auto const* ptr    = evs.data();
    auto const  nbytes = evs.size() * sizeof(event_type);
    auto const  result = ::write(fd, static_cast<void const*>(ptr), nbytes);
    return result == static_cast<ssize_t>(nbytes);
}

bool fs8::capture_binary_format::write_footer(int) const noexcept {
    return true;
}

// ── capture_evtest_format ────────────────────────────────────────────────────

bool fs8::capture_evtest_format::write_header(int const fd) const noexcept {
    constexpr std::string_view header = "# foresight capture evtest\n";
    auto const result = ::write(fd, header.data(), header.size());
    return result == static_cast<ssize_t>(header.size());
}

bool fs8::capture_evtest_format::emit(int const fd, std::span<event_type const> const evs) const noexcept {
    constexpr std::size_t buf_size = 128;
    std::array<char, buf_size> buf{};

    default_evtest_format formatter;
    for (auto const& event : evs) {
        auto const text = formatter.format(event, buf);
        if (text.empty()) {
            continue;
        }
        auto const result = ::write(fd, text.data(), text.size());
        if (result != static_cast<ssize_t>(text.size())) {
            return false;
        }
    }
    return true;
}

bool fs8::capture_evtest_format::write_footer(int) const noexcept {
    return true;
}
