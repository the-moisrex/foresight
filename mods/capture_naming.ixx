// Created by moisrex on 9/4/26.

module;
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
export module fs8.mods:capture_naming;
import fs8.traits;

export namespace fs8 {

    /// Concept for capture file naming strategies.
    ///
    /// `filename(ext)` generates the current output file path with the given extension.
    /// `should_rotate(last_rotation)` returns true when a new file should be
    /// started.  `last_rotation` is the timestamp when the current file was
    /// opened (seconds since epoch).
    template <typename T>
    concept capture_naming = requires(T const& t, std::int64_t last_rotation, std::string_view ext) {
        { t.filename(ext) } -> std::convertible_to<std::string>;
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
    } // namespace detail

    // ── Single file (no rotation) ────────────────────────────────────────────

    /// One file for the entire capture session. No rotation.
    struct [[nodiscard]] capture_single_file : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        [[nodiscard]] static std::string filename(std::string_view ext) noexcept;
        [[nodiscard]] static constexpr bool should_rotate(std::int64_t) noexcept {
            return false;
        }
    };

    static_assert(capture_naming<capture_single_file>);

    // ── Uptime-based ─────────────────────────────────────────────────────────

    /// Filename includes monotonic timestamp. One file per capture session.
    struct [[nodiscard]] capture_uptime : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        [[nodiscard]] static std::string filename(std::string_view ext) noexcept;
        [[nodiscard]] static constexpr bool should_rotate(std::int64_t) noexcept {
            return false;
        }
    };

    static_assert(capture_naming<capture_uptime>);

    // ── Hourly ───────────────────────────────────────────────────────────────

    /// New file every hour.
    struct [[nodiscard]] capture_hourly : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        [[nodiscard]] static std::string filename(std::string_view ext) noexcept;
        [[nodiscard]] static bool should_rotate(std::int64_t last_rotation) noexcept;
    };

    static_assert(capture_naming<capture_hourly>);

    // ── Daily ────────────────────────────────────────────────────────────────

    /// New file every day. When the day changes, a new file is started.
    struct [[nodiscard]] capture_daily : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        [[nodiscard]] static std::string filename(std::string_view ext) noexcept;
        [[nodiscard]] static bool should_rotate(std::int64_t last_rotation) noexcept;
    };

    static_assert(capture_naming<capture_daily>);

    // ── Weekly ───────────────────────────────────────────────────────────────

    /// New file every ISO week.
    struct [[nodiscard]] capture_weekly : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        [[nodiscard]] static std::string filename(std::string_view ext) noexcept;
        [[nodiscard]] static bool should_rotate(std::int64_t last_rotation) noexcept;
    };

    static_assert(capture_naming<capture_weekly>);

    // ── Monthly ──────────────────────────────────────────────────────────────

    /// New file every month.
    struct [[nodiscard]] capture_monthly : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        [[nodiscard]] static std::string filename(std::string_view ext) noexcept;
        [[nodiscard]] static bool should_rotate(std::int64_t last_rotation) noexcept;
    };

    static_assert(capture_naming<capture_monthly>);

    // ── Manual ───────────────────────────────────────────────────────────────

    /// Explicit start/stop. Only rotates when `stop()` is called.
    /// The filename is user-provided or auto-generated on start.
    struct [[nodiscard]] capture_manual : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        [[nodiscard]] std::string filename(std::string_view ext) const noexcept;
        [[nodiscard]] static constexpr bool should_rotate(std::int64_t) noexcept {
            return false; // only rotates on explicit stop
        }
    };

    static_assert(capture_naming<capture_manual>);

    // ── Duration-based ───────────────────────────────────────────────────────

    /// Rotation based on a chrono duration. Maps to the matching naming strategy.
    /// `>= 30d` → monthly, `>= 7d` → weekly, `>= 1d` → daily, `>= 1h` → hourly, else single_file.
    struct [[nodiscard]] capture_name : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::chrono::seconds interval_seconds{};

      public:
        consteval explicit capture_name(std::chrono::seconds dur) noexcept : interval_seconds{dur} {}

        [[nodiscard]] std::string filename(std::string_view ext) const noexcept;
        [[nodiscard]] bool should_rotate(std::int64_t last_rotation) const noexcept;
    };

    static_assert(capture_naming<capture_name>);

    // ── Default ──────────────────────────────────────────────────────────────

    constexpr auto capture_default_naming = capture_daily{};

    // ── Shorthand objects ────────────────────────────────────────────────────

    constexpr capture_single_file single_file{};
    constexpr capture_uptime      uptime{};
    constexpr capture_hourly      hourly{};
    constexpr capture_daily       daily{};
    constexpr capture_weekly      weekly{};
    constexpr capture_monthly     monthly{};
    constexpr capture_manual      manual{};

} // namespace fs8
