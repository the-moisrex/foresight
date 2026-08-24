// Created by moisrex on 8/20/26.

module;
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
using fs8::SINGLETON_HASH_INIT;
using fs8::SINGLETON_HASH_PRIME;

// ---------------------------------------------------------------------------
// basic_singleton::try_acquire_lock
// ---------------------------------------------------------------------------

namespace {
    /// Try to open and flock a lock file under `dir`.  Returns the fd on
    /// success, -1 on any failure (directory creation, open, or lock).
    int try_lock(std::string_view const dir, std::string_view const hex_name) noexcept {
        std::filesystem::path lock_path;
        try {
            lock_path = std::filesystem::path{std::string{dir}} / (std::string{hex_name} + ".lock");
            std::filesystem::create_directories(lock_path.parent_path());
        } catch (...) {
            return -1;
        }
        int const fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT, 0600);
        if (fd < 0) {
            return -1;
        }
        if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
            ::close(fd);
            return -1;
        }
        return fd;
    }
} // anonymous namespace

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

    // 1. Try the system-wide lock.
    constexpr std::string_view sys_dir = "/run/lock/foresight";

    int const sys_fd = try_lock(sys_dir, hex_view);

    // 2. Try the user-wide lock ($XDG_RUNTIME_DIR/foresight).
    int usr_fd = -1;
    if (auto const *runtime = std::getenv("XDG_RUNTIME_DIR")) {
        auto const user_dir = std::string_view{runtime};
        usr_fd              = try_lock(user_dir, hex_view);
        // If both succeeded, drop the user-wide one — system-wide is preferred.
        if (sys_fd >= 0 && usr_fd >= 0) {
            ::close(usr_fd);
            usr_fd = -1;
        }
    }

    // 3. Pick whichever lock we got.
    if (sys_fd < 0 && usr_fd < 0) {
        log("singleton: another instance is already running or lock directories are inaccessible.");
        return context_action::exit;
    }

    if (sys_fd >= 0) {
        lock_fd = sys_fd;
        log("singleton: acquired system-wide lock.");
    } else {
        lock_fd = usr_fd;
        log("singleton: acquired user-wide lock.");
    }
    return context_action::next;
} catch (...) {
    log("Error while trying to grab a lock.");
    return context_action::exit;
}

// Explicit instantiations for every strategy the user might reach through the
// global constexpr singletons or via operator[].
namespace fs8 {
    template struct basic_singleton<exe_hash_solution>;
    template struct basic_singleton<named_solution>;
    template struct basic_singleton<pipeline_hash_solution>;
    template struct basic_singleton<intercept_hash_solution>;
} // namespace fs8
