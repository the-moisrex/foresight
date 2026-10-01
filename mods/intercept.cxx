// Created by moisrex on 6/22/24.

module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <sys/poll.h>
#include <utility>
module fs8.mods;
import fs8.hash;
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
    /// A tracked fd entry: caches the source_id, the device identity (sysname
    /// hash) and a copy of the device name — so disconnect reporting never has
    /// to dereference an evdev that `input_manager` may already have erased —
    /// plus the evdev pointer so the hot path never calls source_id_of()
    /// (which does a readlink syscall) or iterates im.devices() (a
    /// linked-list scan).
    struct watched_fd {
        int         fd                    = -1;
        uint32_t    id                    = source_id_none;
        fs8::evdev* dev                   = nullptr;
        /// `ci_hash(device_sysname(dev))` captured at watch time: the stable
        /// identity of the device behind this fd (the fd number itself is
        /// reused by the kernel as soon as the device is closed).
        uint32_t sysname_hash             = 0;
        /// `devices_generation` this cached pointer was last validated
        /// against; every add/remove of a device bumps the generation.
        uint32_t             validated_at = 0;
        std::array<char, 64> name{};
        uint8_t              name_len = 0;
        bool                 dead     = false;

        constexpr watched_fd() noexcept = default;

        constexpr watched_fd(int f, uint32_t i, fs8::evdev* d, bool ddd = false) noexcept : fd{f}, id{i}, dev{d}, dead{ddd} {}

        void set_name(std::string_view const src) noexcept {
            name_len = static_cast<uint8_t>(std::min(src.size(), name.size() - 1));
            std::copy_n(src.begin(), static_cast<std::ptrdiff_t>(name_len), name.begin());
            name[name_len] = '\0';
        }

        [[nodiscard]] std::string_view name_view() const noexcept {
            return {name.data(), name_len};
        }
    };
} // namespace

template <>
struct fs8::pimpl_idiom<basic_interceptor>::impl {
    basic_input_manager*       im = nullptr;
    std::list<evdev>           manual_devs;
    std::deque<event_type>     pending;
    std::array<watched_fd, 16> watched{};
    std::size_t                watched_count = 0;
    std::array<char, 64>       first_disconnect_name{};
    std::size_t                first_disconnect_name_len = 0;
    std::size_t                disconnect_count          = 0;
    /// Sysname hashes of devices evicted as dead during reconciliation.  The
    /// "watch new devices" phase skips these so a device whose fd just got
    /// POLLERR is not re-watched before udev's "remove" event arrives.  Keyed
    /// by identity rather than fd so a *new* device that reused the dead
    /// device's fd number is still watched.
    std::array<uint32_t, 16> dead_names{};
    std::size_t              dead_name_count = 0;
    /// Last-seen value of input_manager::devices_generation().  When it has
    /// not changed since the last reconciliation and there are no disconnects,
    /// do_pop can skip the entire slow path.
    uint32_t last_generation                 = 0;

    /// Record a disconnect for the batch log in `do_pop`.  Reads the name
    /// copied at watch time: the evdev itself may already be freed.
    void note_disconnect(watched_fd const& entry) noexcept {
        if (disconnect_count == 0) {
            first_disconnect_name_len = entry.name_len;
            std::ranges::copy_n(entry.name.begin(), static_cast<std::ptrdiff_t>(entry.name_len), first_disconnect_name.begin());
            first_disconnect_name[first_disconnect_name_len] = '\0';
        }
        ++disconnect_count;
    }

    /// Whether `entry`'s cached evdev pointer is still the device it was
    /// cached for.  Only called when devices_generation changed since the
    /// entry was validated: any list mutation (which is the only way a
    /// pointer can die) bumps it.
    [[nodiscard]] bool is_live(basic_input_manager& manager, watched_fd const& entry) noexcept {
        for (auto& dev : manager.devices()) {
            if (std::addressof(dev) != entry.dev) {
                continue;
            }
            // A freed node's address can be handed to a *different* device;
            // the sysname is the real identity.
            return ci_hash(std::string_view{device_sysname(dev)}) == entry.sysname_hash;
        }
        return false;
    }
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
        if (entry.dead || entry.dev == nullptr) [[unlikely]] {
            // Already invalidated (e.g. udev removed the device earlier in
            // this very dispatch): drop the registration and touch nothing.
            fd.unwatch = true;
            return next;
        }
        // `input_manager`'s udev monitor fd is registered first, so its
        // handler runs before ours inside the same `io_manager` dispatch and
        // may have erased the device we cache a pointer to.  Every list
        // mutation bumps devices_generation, so a matching generation means
        // the pointer is still live; otherwise re-validate before deref.
        if (auto const generation = pimpl->im->devices_generation(); generation != entry.validated_at) [[unlikely]] {
            if (!pimpl->is_live(*pimpl->im, entry)) {
                pimpl->im->unregister_source(entry.id);
                entry.dead = true;
                entry.dev  = nullptr;
                pimpl->note_disconnect(entry);
                fd.unwatch = true;
                return next;
            }
            entry.validated_at = generation;
        }
        if ((std::to_underlying(fd.revents) & (POLLERR | POLLHUP | POLLNVAL)) != 0) [[unlikely]] {
            pimpl->note_disconnect(entry);
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
    auto const generation = im.devices_generation();
    if (pimpl->disconnect_count == 0 && generation == pimpl->last_generation) [[likely]] {
        return std::nullopt;
    }

    // Build a flat lookup table of (fd, evdev*, identity) from the linked
    // list once, avoiding repeated O(n) list scans for each watched fd.
    struct fd_entry {
        int         fd;
        fs8::evdev* dev;
        uint32_t    sysname_hash;
    };

    std::array<fd_entry, 16> device_table{};
    std::size_t              device_count = 0;
    for (auto& dev : im.devices()) {
        if (device_count < device_table.size()) {
            device_table[device_count++] = {
              .fd           = dev.native_handle(),
              .dev          = std::addressof(dev),
              .sysname_hash = fs8::ci_hash(std::string_view{fs8::device_sysname(dev)}),
            };
        }
    }

    // Helper: find a device by fd in the flat table (O(n) but n is small).
    auto find_device = [&](int fd) noexcept -> fd_entry* {
        for (std::size_t i = 0; i < device_count; ++i) {
            if (device_table[i].fd == fd) {
                return &device_table[i];
            }
        }
        return nullptr;
    };

    // Reconcile watches: evict dead/gone entries, refresh cached pointers,
    // and watch new devices in a single combined pass.
    pimpl->dead_name_count = 0;

    // Pass A: for each live watched entry, find it in device_table, mark that
    // device as tracked, and refresh the cached pointer.  Entries that are
    // dead or whose device vanished are left unmarked for eviction.
    std::array<bool, 16> device_tracked{};
    std::size_t          write = 0;
    for (std::size_t read = 0; read < pimpl->watched_count; ++read) {
        auto& entry       = pimpl->watched[read];
        // The fd alone is not an identity: after a removal the kernel hands
        // the same fd number to the next device, which would otherwise
        // silently inherit this entry's source_id.
        auto* const live  = find_device(entry.fd);
        bool const  alive = live != nullptr && !entry.dead && live->sysname_hash == entry.sysname_hash;
        if (!alive) {
            io.unwatch(entry.fd);
            pimpl->im->unregister_source(entry.id);
            if (pimpl->dead_name_count < pimpl->dead_names.size()) {
                pimpl->dead_names[pimpl->dead_name_count++] = entry.sysname_hash;
            }
            continue;
        }
        // Mark this device as already tracked.
        device_tracked[static_cast<std::size_t>(live - device_table.data())] = true;
        entry.dev                                                            = live->dev;
        entry.validated_at                                                   = generation;
        // Re-register unconditionally — "only if the pointer changed" is not
        // safe: a remove+add can place a *different* device at the same
        // address (list-node reuse) with the same fd and sysname, so pointer
        // equality proves nothing about object identity.  source_map is
        // cleared on erase, and for these mod-prefixed source_ids there is no
        // fallback: device_of() can only resolve them through this map.
        im.register_source(entry.id, *live->dev);
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
        for (std::size_t i = 0; i < pimpl->dead_name_count; ++i) {
            if (pimpl->dead_names[i] == device_table[d].sysname_hash) {
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
            auto const src_id  = sid(intercept, static_cast<uint16_t>(pimpl->watched_count));
            auto&      entry   = pimpl->watched[pimpl->watched_count++];
            entry              = watched_fd{dev_fd, src_id, &dev};
            entry.sysname_hash = device_table[d].sysname_hash;
            entry.validated_at = generation;
            entry.set_name(dev.device_name());
            im.register_source(src_id, dev);
            log("Device '{}' (re)connected.", entry.name_view());
        }
    }

    pimpl->last_generation = generation;

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
