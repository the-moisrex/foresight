// Created by moisrex on 7/18/26.

module;
#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <poll.h>
#include <ranges>
#include <vector>
module fs8.mods.device_registry;
import fs8.devices.udev;
import fs8.context;
import fs8.log;

using fs8::basic_device_registry;
using fs8::context_action;

namespace {

    pollfd get_pollfd(fs8::evdev const& dev) {
        return pollfd{dev.native_handle(), POLLIN, 0};
    }

    auto get_pollfds(auto const& devs) {
        return devs | std::views::transform(get_pollfd) | std::ranges::to<std::vector>();
    }

    void rebuild_pollfds(std::vector<pollfd>& fds, auto const& devs, fs8::udev_monitor* mon) {
        fds = get_pollfds(devs);
        if (mon != nullptr && mon->is_valid()) {
            fds.push_back(pollfd{mon->file_descriptor(), POLLIN, 0});
        }
    }
} // namespace

struct [[nodiscard]] basic_device_registry::impl {
    udev_monitor              monitor;
    std::vector<evdev>        devs;
    std::vector<device_query> queries;
    std::vector<pollfd>       fds;
};

void basic_device_registry::add(evdev&& inp_dev) {
    pimpl->devs.emplace_back(std::move(inp_dev));
}

void basic_device_registry::add(device_query const& inp_query) {
    pimpl->queries.emplace_back(std::move(inp_query));
}

std::span<fs8::evdev const> basic_device_registry::devices() const noexcept {
    return std::span{pimpl->devs};
}

std::span<fs8::evdev> basic_device_registry::devices() noexcept {
    return std::span{pimpl->devs};
}

context_action basic_device_registry::operator()(start_tag) {
    using enum context_action;

    if (pimpl.get() != nullptr) {
        return next;
    }

    pimpl = nullable_indirect<impl>::make();

    if (!pimpl->monitor.is_valid()) [[unlikely]] {
        log("Cannot start monitoring.");
        return exit;
    }

    pimpl->monitor = udev_monitor{};
    pimpl->fds     = get_pollfds(devices());


    for (auto const& cur_query : pimpl->queries) {
        if (!cur_query.fail_on_no_match) {
            continue;
        }

        auto const are_matched = std::bind_back(static_cast<bool (*)(evdev const&, device_query const&)>(matches), cur_query);
        if (!std::ranges::any_of(pimpl->devs, are_matched)) [[unlikely]] {
            log("Needed this query but didn't found it: {}", to_string(cur_query));
            return exit;
        }
    }

    pimpl->monitor.enable();

    return next;
}
