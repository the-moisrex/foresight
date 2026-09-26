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
import fs8.log;

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
    /// A tracked fd entry: caches the source_id, evdev pointer, and a copy of
    /// the device name so the hot path never calls source_id_of() (readlink)
    /// or iterates the device list, and disconnect logging never touches a
    /// pointer that input_manager may already have erased.
    struct watched_fd {
        int                  fd   = -1;
        std::uint32_t        id   = source_id_none;
        fs8::evdev*          dev  = nullptr;
        bool                 dead = false;
        std::array<char, 64> name{};

        constexpr watched_fd() noexcept = default;

        watched_fd(int f, std::uint32_t i, fs8::evdev* d, std::string_view const device_name, bool ddd = false) noexcept
          : fd{f},
            id{i},
            dev{d},
            dead{ddd} {
            auto const len = std::min(device_name.size(), name.size() - 1);
            std::ranges::copy_n(device_name.begin(), static_cast<std::ptrdiff_t>(len), name.begin());
            name[len] = '\0';
        }

        [[nodiscard]] constexpr std::string_view device_name() const noexcept {
            return {name.data()};
        }
    };
} // namespace

template <>
struct fs8::pimpl_idiom<basic_interceptor>::impl {
    std::list<evdev>           manual_devs; // todo: maybe use std::hive?
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
    /// Whether the device list may have changed since the last reconciliation.
    /// Starts true so the first pop always builds the watch table; set again
    /// whenever input_manager broadcasts `devices_changed`.
    bool dirty                        = true;
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

void basic_interceptor::mark_dirty() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->dirty = true;
}

context_action basic_interceptor::do_start() noexcept try {
    using enum context_action;
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }

    // Queries stay owned here; register as a provider so `input_manager` can
    // pull them again (e.g. on `requery`) instead of copying them over.
    auto handle = provider_handle(*this);
    if (auto const res = dynamic_context.broadcast(register_query_provider + &handle); res != next) [[unlikely]] {
        log("interceptor: query registration failed.");
        return res;
    }

    for (auto& dev : pimpl->manual_devs) {
        if (auto const res = dynamic_context.broadcast(add_evdev_device + &dev); res != next) [[unlikely]] {
            log("interceptor: adding device failed.");
            return res;
        }
    }
    pimpl->manual_devs.clear();

    return next;
} catch (...) {
    return context_action::exit;
}

context_action basic_interceptor::operator()(io_fd& fd) noexcept try {
    using enum context_action;
    if (pimpl.get() == nullptr) [[unlikely]] {
        return next;
    }
    // Table lookup: find the watched_fd entry for fd — no device-list iteration.
    for (std::size_t i = 0; i < pimpl->watched_count; ++i) {
        auto& entry = pimpl->watched[i];
        if (entry.fd != fd.fd) {
            continue;
        }
        if ((std::to_underlying(fd.revents) & (POLLERR | POLLHUP | POLLNVAL)) != 0) [[unlikely]] {
            if (pimpl->disconnect_count == 0) [[likely]] {
                // Use the name cached at watch time: input_manager may have
                // already erased the evdev this entry points at.
                auto const name = entry.device_name();
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

std::optional<event_type> basic_interceptor::do_pop(context_action& action) noexcept try {
    using enum context_action;
    if (pimpl.get() == nullptr) [[unlikely]] {
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
    if (pimpl->disconnect_count == 0 && !pimpl->dirty) [[likely]] {
        return std::nullopt;
    }

    // Snapshot the device list into a stack buffer (no callbacks, no heap).
    auto snap = tracked_devices(dynamic_context);
    if (!snap) [[unlikely]] {
        action = snap.action();
        return std::nullopt;
    }

    // Helper: find a device by fd in the snapshot (O(n) but n is small).
    auto find_device = [&](int const fd) noexcept -> evdev* {
        for (evdev* dev : snap) {
            if (dev->native_handle() == fd) {
                return dev;
            }
        }
        return nullptr;
    };

    // Reconcile watches: evict dead/gone entries, refresh cached pointers,
    // and watch new devices in a single combined pass.
    pimpl->dead_fd_count = 0;

    // Pass A: for each live watched entry, find it in the snapshot, mark that
    // device as tracked, and refresh the cached pointer.  Entries that are
    // dead or whose device vanished are left unmarked for eviction.
    std::array<bool, 16> device_tracked{};
    std::size_t          write = 0;
    for (std::size_t read = 0; read < pimpl->watched_count; ++read) {
        auto&      entry    = pimpl->watched[read];
        auto*      live_dev = find_device(entry.fd);
        bool const alive    = live_dev != nullptr && !entry.dead;
        if (!alive) {
            int gone_fd            = entry.fd;
            std::ignore            = dynamic_context.broadcast(io_unwatch + &gone_fd);
            std::uint32_t unreg_id = entry.id;
            if (auto const res = dynamic_context.broadcast(source_unregistered + &unreg_id); is_exiting(res)) {
                action = res;
                return std::nullopt;
            }
            if (pimpl->dead_fd_count < pimpl->dead_fds.size()) {
                pimpl->dead_fds[pimpl->dead_fd_count++] = entry.fd;
            }
            continue;
        }
        // Mark this device as already tracked.
        for (std::size_t d = 0; d < snap.size(); ++d) {
            if (snap[d] == live_dev) {
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
    for (std::size_t d = 0; d < snap.size(); ++d) {
        if (device_tracked[d]) {
            continue;
        }
        auto&     dev     = *snap[d];
        int const dev_fd  = dev.native_handle();
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
        auto req    = watch_of(io_fd{.fd = dev_fd, .events = io_event::in}, *this);
        std::ignore = dynamic_context.broadcast(io_watch + &req);
        if (req.status == io_watch_status::registered) {
            auto const src_id                      = sid(intercept, static_cast<std::uint16_t>(pimpl->watched_count));
            auto const dname                       = dev.device_name();
            pimpl->watched[pimpl->watched_count++] = watched_fd{dev_fd, src_id, &dev, dname};
            source_registration reg{src_id, &dev};
            if (auto const res = dynamic_context.broadcast(source_registered + &reg); fs8::is_exiting(res)) {
                action = res;
                return std::nullopt;
            }
            log("Device '{}' (re)connected.", dname);
        }
    }

    pimpl->dirty = false;

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
