// Created by moisrex on 6/22/24.

module;
#include <algorithm>
#include <deque>
#include <list>
#include <optional>
#include <span>
#include <sys/poll.h>
#include <utility>
#include <vector>
module fs8.mods.intercept;
import fs8.devices.evdev;
import fs8.context;
import fs8.log;
import fs8.mods.io_manager;
import fs8.mods.input_manager;

using fs8::basic_interceptor;
using fs8::context_action;
using fs8::device_query;
using fs8::event_type;
using fs8::io_event;
using fs8::io_fd;
using fs8::provider_handle;

template <>
struct fs8::pimpl_idiom<basic_interceptor>::impl {
    basic_input_manager*   im = nullptr;
    std::list<evdev>       manual_devs;
    std::deque<event_type> pending;
    std::vector<int>       watched_fds;
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

    // log("DEBUG do_start: queries_count={}", queries_count);

    // Queries stay owned here; register as a provider so `input_manager` can
    // pull them again (e.g. on `requery`) instead of copying them over.
    im.add_query_provider(provider_handle(*this));

    for (auto& dev : pimpl->manual_devs) {
        im.add(std::move(dev));
    }
    pimpl->manual_devs.clear();

    return im.start(io);
} catch (...) {
    return context_action::exit;
}

context_action basic_interceptor::operator()(io_fd const& fd) noexcept try {
    using enum context_action;
    if (pimpl.get() == nullptr || pimpl->im == nullptr) [[unlikely]] {
        return next;
    }
    for (auto& dev : pimpl->im->devices()) {
        if (dev.native_handle() != fd.fd) {
            continue;
        }
        if ((std::to_underlying(fd.revents) & (POLLERR | POLLHUP | POLLNVAL)) != 0) [[unlikely]] {
            log("Device '{}' error/disconnected.", dev.device_name());
            return next;
        }
        // Stamp each event with the source device's opaque id (a hash of its
        // sysname). Whether the device is a real one, our own uinput device,
        // or another process's foresight virtual device is answered later by
        // input_manager (`is_owned` / `is_chained`).
        auto const source = pimpl->im->device_id_of(dev);
        while (auto const ev = dev.next()) {
            pimpl->pending.emplace_back(*ev).source(source);
        }
        break;
    }
    return next;
} catch (...) {
    return context_action::next;
}

std::optional<event_type> basic_interceptor::do_pop(basic_input_manager& im, basic_io_manager& io) noexcept try {
    if (pimpl.get() == nullptr || pimpl->im == nullptr) [[unlikely]] {
        return std::nullopt;
    }

    // Unwatch fds whose devices are gone (hotplug removals).
    for (auto it = pimpl->watched_fds.begin(); it != pimpl->watched_fds.end();) {
        bool const found = std::ranges::any_of(im.devices(), [fd = *it](evdev const& dev) noexcept {
            return dev.native_handle() == fd;
        });
        if (!found) {
            io.unwatch(*it);
            it = pimpl->watched_fds.erase(it);
        } else {
            ++it;
        }
    }

    // Watch any device that is not watched yet (startup + hotplug adds).
    for (auto& dev : im.devices()) {
        int const fd = dev.native_handle();
        if (io.is_watched(fd)) {
            continue;
        }
        if (io.watch(io_fd{.fd = fd, .events = io_event::in}, *this)) {
            pimpl->watched_fds.push_back(fd);
        }
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
