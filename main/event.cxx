// Created by moisrex on 8/17/26.

module;
#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>
module fs8.event;

[[nodiscard]] std::string_view fs8::to_string(device_id const id) noexcept {
    using enum device_id;
    switch (id) {
        case none: return {"none"};
        case stdin: return {"stdin"};
        case self: return {"self"};
        case scheduler: return {"scheduler"};
        default: {
            // Device hashes are resolved to their sysname via input_manager;
            // here we only have the opaque id, so print it in hex. The buffer
            // is thread-local to keep this allocation-free.
            static thread_local std::array<char, 32> buf{};
            auto const                               first = buf.data();
            auto const                               mid   = std::copy(std::begin("device:"), std::end("device:") - 1, first);
            auto const [ptr, ec] = std::to_chars(mid, buf.data() + buf.size() - 1, static_cast<std::uint32_t>(id), 16);
            if (ec == std::errc{}) [[likely]] {
                return std::string_view{first, static_cast<std::size_t>(ptr - first)};
            }
            return {"<unknown>"};
        }
    }
}
