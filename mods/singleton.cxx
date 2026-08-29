// Created by moisrex on 8/20/26.

module;
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
module fs8.mods;
import fs8.log;

using fs8::basic_singleton;
using fs8::context_action;

// ---------------------------------------------------------------------------
// basic_singleton::try_acquire_lock
// ---------------------------------------------------------------------------

namespace {
    /// Bind an abstract Unix domain socket.  Returns the fd on success,
    /// -1 on any failure.  The abstract namespace lives purely in kernel
    /// memory: no filesystem permissions are needed and the kernel
    /// automatically frees the socket when the process terminates.
    int try_lock(std::string_view const hex_name) noexcept {
        int const fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }

        sockaddr_un addr{};
        addr.sun_family                   = AF_UNIX;
        // First byte '\0' designates the abstract namespace.
        addr.sun_path[0]                  = '\0';
        constexpr std::string_view prefix = "foresight-";
        std::memcpy(addr.sun_path + 1, prefix.data(), prefix.size());
        std::memcpy(addr.sun_path + 1 + prefix.size(), hex_name.data(), hex_name.size());

        // Length = family + '\0' + prefix + hex_name
        auto const len = static_cast<socklen_t>(sizeof(addr.sun_family) + 1 + prefix.size() + hex_name.size());

        if (::bind(fd, reinterpret_cast<sockaddr const *>(&addr), len) < 0) {
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
        log("singleton: failed to format lock name.");
        return context_action::exit;
    }
    std::string_view const hex_view{hex.data(), static_cast<std::size_t>(ptr - hex.data())};

    int const fd = try_lock(hex_view);
    if (fd < 0) {
        log("singleton: another instance is already running on this system.");
        return context_action::exit;
    }

    lock_fd = fd;
    log("singleton: acquired system-wide lock.");
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
