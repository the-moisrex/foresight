// Created by moisrex on 9/4/26.

module;
#include <chrono>
#include <cstdint>
#include <ctime>
#include <format>
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

// ── capture_single_file ──────────────────────────────────────────────────────

std::string fs8::capture_single_file::filename(std::string_view const ext) noexcept {
    auto const now = detail::local_time_now();
    return std::format("capture-{:04d}{:02d}{:02d}{}", now.year, now.month, now.day, ext);
}

// ── capture_uptime ───────────────────────────────────────────────────────────

std::string fs8::capture_uptime::filename(std::string_view const ext) noexcept {
    auto const now = detail::local_time_now();
    return std::format("capture-{:04d}{:02d}{:02d}-{:02d}0000{}", now.year, now.month, now.day, now.hour, ext);
}

// ── capture_hourly ───────────────────────────────────────────────────────────

std::string fs8::capture_hourly::filename(std::string_view const ext) noexcept {
    auto const now = detail::local_time_now();
    return std::format("capture-{:04d}-{:02d}-{:02d}-{:02d}{}", now.year, now.month, now.day, now.hour, ext);
}

bool fs8::capture_hourly::should_rotate(std::int64_t const last_rotation) noexcept {
    auto const cur = detail::local_time_now();
    auto const old = detail::time_from_epoch(last_rotation);
    return cur.year != old.year || cur.month != old.month || cur.day != old.day || cur.hour != old.hour;
}

// ── capture_daily ────────────────────────────────────────────────────────────

std::string fs8::capture_daily::filename(std::string_view const ext) noexcept {
    auto const now = detail::local_time_now();
    return std::format("capture-{:04d}-{:02d}-{:02d}{}", now.year, now.month, now.day, ext);
}

bool fs8::capture_daily::should_rotate(std::int64_t const last_rotation) noexcept {
    auto const cur = detail::local_time_now();
    auto const old = detail::time_from_epoch(last_rotation);
    return cur.year != old.year || cur.month != old.month || cur.day != old.day;
}

// ── capture_weekly ───────────────────────────────────────────────────────────

std::string fs8::capture_weekly::filename(std::string_view const ext) noexcept {
    auto const now = detail::local_time_now();
    return std::format("capture-{:04d}-W{:02d}{}", now.year, now.week, ext);
}

bool fs8::capture_weekly::should_rotate(std::int64_t const last_rotation) noexcept {
    auto const cur = detail::local_time_now();
    auto const old = detail::time_from_epoch(last_rotation);
    return cur.year != old.year || cur.week != old.week;
}

// ── capture_monthly ──────────────────────────────────────────────────────────

std::string fs8::capture_monthly::filename(std::string_view const ext) noexcept {
    auto const now = detail::local_time_now();
    return std::format("capture-{:04d}-{:02d}{}", now.year, now.month, ext);
}

bool fs8::capture_monthly::should_rotate(std::int64_t const last_rotation) noexcept {
    auto const cur = detail::local_time_now();
    auto const old = detail::time_from_epoch(last_rotation);
    return cur.year != old.year || cur.month != old.month;
}

// ── capture_manual ───────────────────────────────────────────────────────────

std::string fs8::capture_manual::filename(std::string_view const ext) const noexcept {
    return std::format("capture{}", ext);
}

// ── capture_name (duration-based) ────────────────────────────────────────────

std::string fs8::capture_name::filename(std::string_view const ext) const noexcept {
    auto const now = detail::local_time_now();
    if (interval_seconds.count() >= 30 * 86400) {
        return std::format("capture-{:04d}-{:02d}{}", now.year, now.month, ext);
    }
    if (interval_seconds.count() >= 7 * 86400) {
        return std::format("capture-{:04d}-W{:02d}{}", now.year, now.week, ext);
    }
    if (interval_seconds.count() >= 86400) {
        return std::format("capture-{:04d}-{:02d}-{:02d}{}", now.year, now.month, now.day, ext);
    }
    if (interval_seconds.count() >= 3600) {
        return std::format("capture-{:04d}-{:02d}-{:02d}-{:02d}{}", now.year, now.month, now.day, now.hour, ext);
    }
    return std::format("capture-{:04d}{:02d}{:02d}-{:02d}0000{}", now.year, now.month, now.day, now.hour, ext);
}

bool fs8::capture_name::should_rotate(std::int64_t const last_rotation) const noexcept {
    auto const now_seconds = detail::now_epoch_seconds();
    return std::chrono::seconds{now_seconds - last_rotation} >= interval_seconds;
}
