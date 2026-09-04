// Created by moisrex on 9/4/26.

module;
#include <cstdint>
#include <ctime>
#include <string>
module fs8.mods;

// ── detail helpers ───────────────────────────────────────────────────────────

fs8::detail::tm_info fs8::detail::local_time_now() noexcept {
    std::time_t const now = std::time(nullptr);
    struct tm         t{};
    localtime_r(&now, &t);
    return {
      .year  = t.tm_year + epoch_year_offset,
      .month = t.tm_mon + 1,
      .day   = t.tm_mday,
      .hour  = t.tm_hour,
      .week  = (t.tm_yday / days_per_week) + 1,
    };
}

std::int64_t fs8::detail::now_epoch_seconds() noexcept {
    return static_cast<std::int64_t>(std::time(nullptr));
}

fs8::detail::tm_info fs8::detail::time_from_epoch(std::int64_t const epoch) noexcept {
    std::time_t const t = static_cast<std::time_t>(epoch);
    struct tm         tm{};
    localtime_r(&t, &tm);
    return {
      .year  = tm.tm_year + epoch_year_offset,
      .month = tm.tm_mon + 1,
      .day   = tm.tm_mday,
      .hour  = tm.tm_hour,
      .week  = (tm.tm_yday / days_per_week) + 1,
    };
}

std::string fs8::detail::zero_pad(int const value, int const width) noexcept {
    auto const s = std::to_string(value);
    if (static_cast<int>(s.size()) >= width) {
        return s;
    }
    return std::string(static_cast<std::size_t>(width) - s.size(), '0') + s;
}

// ── capture_single_file ──────────────────────────────────────────────────────

std::string fs8::capture_single_file::filename() const noexcept {
    auto const now = detail::local_time_now();
    return prefix + "-" + detail::zero_pad(now.year, 4) + detail::zero_pad(now.month, 2)
         + detail::zero_pad(now.day, 2) + ".bin";
}

// ── capture_uptime ───────────────────────────────────────────────────────────

std::string fs8::capture_uptime::filename() const noexcept {
    auto const now = detail::local_time_now();
    return "capture-" + detail::zero_pad(now.year, 4) + detail::zero_pad(now.month, 2)
         + detail::zero_pad(now.day, 2) + "-" + detail::zero_pad(now.hour, 2) + "0000" + ".bin";
}

// ── capture_hourly ───────────────────────────────────────────────────────────

std::string fs8::capture_hourly::filename() const noexcept {
    auto const now = detail::local_time_now();
    return "capture-" + detail::zero_pad(now.year, 4) + "-" + detail::zero_pad(now.month, 2) + "-"
         + detail::zero_pad(now.day, 2) + "-" + detail::zero_pad(now.hour, 2) + ".bin";
}

bool fs8::capture_hourly::should_rotate(std::int64_t const last_rotation) const noexcept {
    auto const cur = detail::local_time_now();
    auto const old = detail::time_from_epoch(last_rotation);
    return cur.year != old.year || cur.month != old.month || cur.day != old.day || cur.hour != old.hour;
}

// ── capture_daily ────────────────────────────────────────────────────────────

std::string fs8::capture_daily::filename() const noexcept {
    auto const now = detail::local_time_now();
    return "capture-" + detail::zero_pad(now.year, 4) + "-" + detail::zero_pad(now.month, 2) + "-"
         + detail::zero_pad(now.day, 2) + ".bin";
}

bool fs8::capture_daily::should_rotate(std::int64_t const last_rotation) const noexcept {
    auto const cur = detail::local_time_now();
    auto const old = detail::time_from_epoch(last_rotation);
    return cur.year != old.year || cur.month != old.month || cur.day != old.day;
}

// ── capture_weekly ───────────────────────────────────────────────────────────

std::string fs8::capture_weekly::filename() const noexcept {
    auto const now = detail::local_time_now();
    return "capture-" + detail::zero_pad(now.year, 4) + "-W" + detail::zero_pad(now.week, 2) + ".bin";
}

bool fs8::capture_weekly::should_rotate(std::int64_t const last_rotation) const noexcept {
    auto const cur = detail::local_time_now();
    auto const old = detail::time_from_epoch(last_rotation);
    return cur.year != old.year || cur.week != old.week;
}

// ── capture_monthly ──────────────────────────────────────────────────────────

std::string fs8::capture_monthly::filename() const noexcept {
    auto const now = detail::local_time_now();
    return "capture-" + detail::zero_pad(now.year, 4) + "-" + detail::zero_pad(now.month, 2) + ".bin";
}

bool fs8::capture_monthly::should_rotate(std::int64_t const last_rotation) const noexcept {
    auto const cur = detail::local_time_now();
    auto const old = detail::time_from_epoch(last_rotation);
    return cur.year != old.year || cur.month != old.month;
}

// ── capture_manual ───────────────────────────────────────────────────────────

std::string fs8::capture_manual::filename() const noexcept {
    return base_name + ".bin";
}
