// Created by moisrex on 6/22/24.

module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <list>
#include <optional>
#include <span>
#include <string_view>
#include <sys/poll.h>
#include <utility>
module fs8.mods;
import fs8.devices.evdev;
import fs8.context;
import fs8.log;
import :io_manager;
import :input_manager;

using fs8::basic_interceptor;
using fs8::context_action;
using fs8::device_query;
using fs8::event_type;
using fs8::io_event;
using fs8::io_fd;
using fs8::provider_handle;
using fs8::sid;
using fs8::source_id_none;

namespace {
    /// A tracked fd entry: caches the source_id and evdev pointer so the hot path
    /// never calls source_id_of() (which does a readlink syscall) or iterates
    /// im.devices() (a linked-list scan).
    struct watched_fd {
        int           fd   = -1;
        std::uint32_t id   = source_id_none;
        fs8::evdev*   dev  = nullptr;
        bool          dead = false;

        constexpr watched_fd() noexcept = default;

        constexpr watched_fd(int f, std::uint32_t i, fs8::evdev* d, bool ddd = false) noexcept : fd{f}, id{i}, dev{d}, dead{ddd} {}
    };
} // namespace

template <>
struct fs8::pimpl_idiom<basic_interceptor>::impl {
    basic_input_manager*       im = nullptr;
    std::list<fs8::evdev>      manual_devs;
    std::deque<event_type>     pending;
    std::array<watched_fd, 16> watched{};
    std::size_t                watched_count = 0;
    std::array<char, 64>       first_disconnect_name{};
    std::size_t                first_disconnect_name_len = 0;
    std::size_t                disconnect_count          = 0;
    /// Fds evicted as dead during reconciliation.  The "watch new devices"
    /// phase skips these so a device whose fd just got POLLERR is not
    /// re-watched before udev's "remove" event arrives.
    std::array<int, 16> dead_fds{};
    std::size_t         dead_fd_count = 0;
    /// Last-seen value of input_manager::devices_generation().  When it has
    /// not changed since the last reconciliation and there are no disconnects,
    /// do_pop can skip the entire slow path.
    std::uint32_t last_generation     = 0;
};

void basic_interceptor::add(evdev&& dev) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    try {
        pimpl->manual_devs.emplace_back(std::move(dev));
    } catch (...) {}
}

void basic_interceptor::add(device_query const& q) noexcept {
    if (queries_count >= owned_queries.size()) [[unlikely]] {
        return;
    }
    owned_queries[queries_count++].set(q);
}

void basic_interceptor::add(owned_query const& q) noexcept {
    add(static_cast<device_query>(q));
}

std::span<device_query const> basic_interceptor::queries() noexcept {
    for (std::size_t i = 0; i < queries_count; ++i) {
        query_cache[i] = owned_queries[i];
    }
    return {query_cache.data(), queries_count};
}

context_action basic_interceptor::do_start(basic_input_manager& im, basic_io_manager& io) noexcept try {
    using enum context_action;
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }

    pimpl->im = &im;

    // Queries stay owned here; register as a provider so `input_manager` can
    // pull them again (e.g. on `requery`) instead of copying them over.
    im.add_query_provider(provider_handle(*this));

    // If `input_manager` started before us, it already enumerated without any
    // queries registered; re-run the enumeration now that we're a provider
    // (no-op when it hasn't started yet, so both pipeline orderings work).
    im.requery();

    for (auto& dev : pimpl->manual_devs) {
        im.add(std::move(dev));
    }
    pimpl->manual_devs.clear();

    return im.start(io);
} catch (...) {
    return context_action::exit;
}

context_action basic_interceptor::operator()(io_fd& fd) noexcept try {
    using enum context_action;
    if (pimpl->im == nullptr) [[unlikely]] {
        return next;
    }
    // Table lookup: find the watched_fd entry by fd — no device-list iteration.
    for (std::size_t i = 0; i < pimpl->watched_count; ++i) {
        auto& entry = pimpl->watched[i];
        if (entry.fd != fd.fd) {
            continue;
        }
        if ((std::to_underlying(fd.revents) & (POLLERR | POLLHUP | POLLNVAL)) != 0) [[unlikely]] {
            if (pimpl->disconnect_count == 0) [[likely]] {
                auto const name = entry.dev->device_name();
                auto const len  = std::min(name.size(), pimpl->first_disconnect_name.size() - 1);
                std::ranges::copy_n(name.begin(), static_cast<std::ptrdiff_t>(len), pimpl->first_disconnect_name.begin());
                pimpl->first_disconnect_name[len] = '\0';
                pimpl->first_disconnect_name_len  = len;
            }
            ++pimpl->disconnect_count;
            entry.dead = true;
            fd.unwatch = true;
            return next;
        }
        // Drain events using cached source_id — no readlink syscall.
        auto const id = entry.id;
        while (auto const ev = entry.dev->next()) {
            pimpl->pending.emplace_back(*ev).source(id);
        }
        return next;
    }
    return next;
} catch (...) {
    return context_action::next;
}

std::optional<event_type> basic_interceptor::do_pop(basic_input_manager& im, basic_io_manager& io) noexcept try {
    if (pimpl->im == nullptr) [[unlikely]] {
        return std::nullopt;
    }

    // Fast path: drain the pending queue without reconciliation.
    if (pimpl->watched_count > 0 && !pimpl->pending.empty()) [[likely]] {
        auto const ev = pimpl->pending.front();
        pimpl->pending.pop_front();
        return ev;
    }

    // Skip reconciliation when nothing changed: no devices added/removed
    // and no disconnects detected since the last reconciliation.
    if (pimpl->disconnect_count == 0 && im.devices_generation() == pimpl->last_generation) [[likely]] {
        return std::nullopt;
    }

    // Build a flat lookup table of (fd, evdev*) from the linked list once,
    // avoiding repeated O(n) list scans for each watched fd.
    struct fd_entry {
        int         fd;
        fs8::evdev* dev;
    };

    std::array<fd_entry, 16> device_table{};
    std::size_t              device_count = 0;
    for (auto& dev : im.devices()) {
        if (device_count < device_table.size()) {
            device_table[device_count++] = {dev.native_handle(), &dev};
        }
    }

    // Helper: find a device by fd in the flat table (O(n) but n is small).
    auto find_device = [&](int fd) noexcept -> fs8::evdev* {
        for (std::size_t i = 0; i < device_count; ++i) {
            if (device_table[i].fd == fd) {
                return device_table[i].dev;
            }
        }
        return nullptr;
    };

    // Reconcile watches: evict dead/gone entries, refresh cached pointers,
    // and watch new devices in a single combined pass.
    pimpl->dead_fd_count = 0;

    // Pass A: for each live watched entry, find it in device_table, mark that
    // device as tracked, and refresh the cached pointer.  Entries that are
    // dead or whose device vanished are left unmarked for eviction.
    std::array<bool, 16> device_tracked{};
    std::size_t          write = 0;
    for (std::size_t read = 0; read < pimpl->watched_count; ++read) {
        auto&      entry    = pimpl->watched[read];
        auto*      live_dev = find_device(entry.fd);
        bool const alive    = live_dev != nullptr && !entry.dead;
        if (!alive) {
            io.unwatch(entry.fd);
            pimpl->im->unregister_source(entry.id);
            if (pimpl->dead_fd_count < pimpl->dead_fds.size()) {
                pimpl->dead_fds[pimpl->dead_fd_count++] = entry.fd;
            }
            continue;
        }
        // Mark this device as already tracked.
        for (std::size_t d = 0; d < device_count; ++d) {
            if (device_table[d].fd == entry.fd) {
                device_tracked[d] = true;
                break;
            }
        }
        entry.dev = live_dev;
        if (write != read) {
            pimpl->watched[write] = entry;
        }
        ++write;
    }
    pimpl->watched_count = write;

    // Pass B: watch devices not yet tracked.
    for (std::size_t d = 0; d < device_count; ++d) {
        if (device_tracked[d]) {
            continue;
        }
        auto&     dev     = *device_table[d].dev;
        int const dev_fd  = device_table[d].fd;
        bool      is_dead = false;
        for (std::size_t i = 0; i < pimpl->dead_fd_count; ++i) {
            if (pimpl->dead_fds[i] == dev_fd) {
                is_dead = true;
                break;
            }
        }
        if (is_dead) {
            continue;
        }
        if (pimpl->watched_count >= pimpl->watched.size()) [[unlikely]] {
            break;
        }
        if (io.watch(io_fd{.fd = dev_fd, .events = io_event::in}, *this)) {
            auto const src_id                      = sid(intercept, static_cast<std::uint16_t>(pimpl->watched_count));
            pimpl->watched[pimpl->watched_count++] = watched_fd{dev_fd, src_id, &dev};
            im.register_source(src_id, dev);
            log("Device '{}' (re)connected.", dev.device_name());
        }
    }

    pimpl->last_generation = im.devices_generation();

    // Log a batch summary for disconnects detected during the last load_event.
    if (pimpl->disconnect_count > 0) [[unlikely]] {
        auto const name = std::string_view{pimpl->first_disconnect_name.data(), pimpl->first_disconnect_name_len};
        if (pimpl->disconnect_count == 1) {
            log("Device '{}' error/disconnected.", name);
        } else {
            log("Device '{}' and {} other(s) disconnected.", name, pimpl->disconnect_count - 1);
        }
        pimpl->disconnect_count = 0;
    }

    if (pimpl->pending.empty()) [[likely]] {
        return std::nullopt;
    }
    auto const ev = pimpl->pending.front();
    pimpl->pending.pop_front();
    return ev;
} catch (...) {
    return std::nullopt;
}
