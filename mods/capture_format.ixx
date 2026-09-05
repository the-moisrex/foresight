// Created by moisrex on 9/4/26.

module;
#include <cstdint>
#include <span>
#include <string_view>
#include <unistd.h>
export module fs8.mods:capture_format;
import fs8.event;
import fs8.traits;

export namespace fs8 {

    /// Concept for capture output formats.
    template <typename T>
    concept capture_format = requires(int fd, std::span<event_type const> evs) {
        { T::write_header(fd) } -> std::convertible_to<bool>;
        { T::emit(fd, evs) } -> std::convertible_to<bool>;
        { T::write_footer(fd) } -> std::convertible_to<bool>;
    };

    /// Binary capture: raw input_event structs. Compact, fast, replayable.
    struct [[nodiscard]] capture_binary_format : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        static constexpr std::uint32_t magic = 0x38534646u; // "FFS8" little-endian
        static constexpr std::uint16_t version = 1;
        static constexpr std::string_view extension = ".fs8";

        // NOLINTNEXTLINE(*-use-nodiscard)
        static bool write_header(int fd) noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        static bool emit(int fd, std::span<event_type const> evs) noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        static bool write_footer(int fd) noexcept;
    };

    static_assert(capture_format<capture_binary_format>);

    /// Evtest text capture: human-readable event lines. Slower but inspectable.
    /// Delegates per-event formatting to default_evtest_format.
    struct [[nodiscard]] capture_evtest_format : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        static constexpr std::string_view extension = ".evtest.fs8";

        // NOLINTNEXTLINE(*-use-nodiscard)
        static bool write_header(int fd) noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        static bool emit(int fd, std::span<event_type const> evs) noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        static bool write_footer(int fd) noexcept;
    };

    static_assert(capture_format<capture_evtest_format>);

} // namespace fs8
