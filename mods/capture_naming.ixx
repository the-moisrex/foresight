// Created by moisrex on 9/4/26.

module;
#include <cstdint>
#include <string>
#include <string_view>
export module fs8.mods:capture_naming;
import fs8.traits;

export namespace fs8 {

    /// Concept for capture file naming strategies.
    ///
    /// `filename()` generates the current output file path.
    /// `should_rotate(last_rotation)` returns true when a new file should be
    /// started.  `last_rotation` is the timestamp when the current file was
    /// opened (seconds since epoch).
    template <typename T>
    concept capture_naming = requires(T const& t, std::int64_t last_rotation) {
        { t.filename() } -> std::convertible_to<std::string>;
        { t.should_rotate(last_rotation) } -> std::convertible_to<bool>;
    };

    // ── Helpers ──────────────────────────────────────────────────────────────

    namespace detail {
        constexpr int epoch_year_offset = 1900;
        constexpr int days_per_week     = 7;

        struct [[nodiscard]] tm_info {
            int year  = 0;
            int month = 0;
            int day   = 0;
            int hour  = 0;
            int week  = 0; // ISO week of year
        };

        [[nodiscard]] tm_info local_time_now() noexcept;
        [[nodiscard]] std::int64_t now_epoch_seconds() noexcept;
        [[nodiscard]] tm_info time_from_epoch(std::int64_t epoch) noexcept;
        [[nodiscard]] std::string zero_pad(int value, int width) noexcept;
    } // namespace detail

    // ── Single file (no rotation) ────────────────────────────────────────────

    /// One file for the entire capture session. No rotation.
    struct [[nodiscard]] capture_single_file : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        std::string prefix = "capture";

        [[nodiscard]] std::string filename() const noexcept;
        [[nodiscard]] constexpr bool should_rotate(std::int64_t) const noexcept {
            return false;
        }
    };

    static_assert(capture_naming<capture_single_file>);

    // ── Uptime-based ─────────────────────────────────────────────────────────

    /// Filename includes monotonic timestamp. One file per capture session.
    struct [[nodiscard]] capture_uptime : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        [[nodiscard]] std::string filename() const noexcept;
        [[nodiscard]] constexpr bool should_rotate(std::int64_t) const noexcept {
            return false;
        }
    };

    static_assert(capture_naming<capture_uptime>);

    // ── Hourly ───────────────────────────────────────────────────────────────

    /// New file every hour.
    struct [[nodiscard]] capture_hourly : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        [[nodiscard]] std::string filename() const noexcept;
        [[nodiscard]] bool should_rotate(std::int64_t last_rotation) const noexcept;
    };

    static_assert(capture_naming<capture_hourly>);

    // ── Daily ────────────────────────────────────────────────────────────────

    /// New file every day. When the day changes, a new file is started.
    struct [[nodiscard]] capture_daily : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        [[nodiscard]] std::string filename() const noexcept;
        [[nodiscard]] bool should_rotate(std::int64_t last_rotation) const noexcept;
    };

    static_assert(capture_naming<capture_daily>);

    // ── Weekly ───────────────────────────────────────────────────────────────

    /// New file every ISO week.
    struct [[nodiscard]] capture_weekly : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        [[nodiscard]] std::string filename() const noexcept;
        [[nodiscard]] bool should_rotate(std::int64_t last_rotation) const noexcept;
    };

    static_assert(capture_naming<capture_weekly>);

    // ── Monthly ──────────────────────────────────────────────────────────────

    /// New file every month.
    struct [[nodiscard]] capture_monthly : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        [[nodiscard]] std::string filename() const noexcept;
        [[nodiscard]] bool should_rotate(std::int64_t last_rotation) const noexcept;
    };

    static_assert(capture_naming<capture_monthly>);

    // ── Manual ───────────────────────────────────────────────────────────────

    /// Explicit start/stop. Only rotates when `stop()` is called.
    /// The filename is user-provided or auto-generated on start.
    struct [[nodiscard]] capture_manual : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        std::string base_name = "capture";

        [[nodiscard]] std::string filename() const noexcept;
        [[nodiscard]] constexpr bool should_rotate(std::int64_t) const noexcept {
            return false;
        }
    };

    static_assert(capture_naming<capture_manual>);

    // ── Default ──────────────────────────────────────────────────────────────

    constexpr auto capture_default_naming = capture_daily{};

} // namespace fs8
