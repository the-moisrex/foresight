// Created by moisrex on 8/20/26.

module;
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <unistd.h>
module fs8.mods;
import fs8.log;

using fs8::basic_singleton;
using fs8::context_action;
using fs8::exe_hash_solution;
using fs8::SINGLETON_HASH_INIT;
using fs8::SINGLETON_HASH_PRIME;

// ---------------------------------------------------------------------------
// exe_hash_solution
// ---------------------------------------------------------------------------

std::uint64_t exe_hash_solution::operator()(auto &) const noexcept {
    std::uint64_t hash = SINGLETON_HASH_INIT;
    try {
        auto const path = std::filesystem::canonical("/proc/self/exe");
        auto const name = path.filename().string();
        for (auto const cur_ch : name) {
            hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(cur_ch));
            hash *= SINGLETON_HASH_PRIME;
        }
    } catch (...) {
        // fallback: hash a fixed string so the pipeline can still run
        for (auto c : std::string_view{"foresight"}) {
            hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
            hash *= SINGLETON_HASH_PRIME;
        }
    }
    return hash;
}

// ---------------------------------------------------------------------------
// basic_singleton::try_acquire_lock
// ---------------------------------------------------------------------------

template <typename Solution>
context_action basic_singleton<Solution>::try_acquire_lock(std::uint64_t const hash) noexcept try {
    // Format the hash as 16 hex characters.
    std::array<char, 16> hex{};
    auto [ptr, ec] = std::to_chars(hex.data(), hex.data() + hex.size(), hash, 16);
    if (ec != std::errc{}) {
        log("singleton: failed to format lock-file name.");
        return context_action::exit;
    }
    std::string_view const hex_view{hex.data(), static_cast<std::size_t>(ptr - hex.data())};

    // Build the lock-file path: <dir>/<hex>.lock
    std::filesystem::path lock_path;
    try {
        lock_path = std::filesystem::path{std::string{lock_dir}} / (std::string{hex_view} + ".lock");
        std::filesystem::create_directories(lock_path.parent_path());
    } catch (...) {
        log("singleton: failed to create lock directory '{}'.", lock_dir);
        return context_action::exit;
    }

    // Open (or create) the lock file.
    int const fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT, 0600);
    if (fd < 0) {
        log("singleton: failed to open lock file '{}': {}.", lock_path.string(), std::strerror(errno));
        return context_action::exit;
    }

    // Try a non-blocking exclusive lock.
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            log("singleton: another instance is already running (lock '{}').", lock_path.string());
        } else {
            log("singleton: failed to acquire lock '{}': {}.", lock_path.string(), std::strerror(errno));
        }
        ::close(fd);
        return context_action::exit;
    }

    lock_fd = fd;
    log("singleton: acquired lock '{}'.", lock_path.string());
    return context_action::next;
} catch (...) {
    log("Error thrown");
    return context_action::exit;
}

// Explicit instantiations for every strategy the user might reach through the
// global constexpr singletons or via operator[].
template struct basic_singleton<fs8::exe_hash_solution>;
template struct basic_singleton<fs8::named_solution>;
template struct basic_singleton<fs8::pipeline_hash_solution>;
template struct basic_singleton<fs8::intercept_hash_solution>;
