// Created by moisrex on 7/18/26.

module;
#include <algorithm>
#include <functional>
#include <poll.h>
#include <ranges>
#include <vector>
module fs8.mods.device_registry;
import fs8.context;
import fs8.log;

using fs8::basic_device_registry;
using fs8::context_action;

namespace {

    pollfd get_pollfd(fs8::evdev const& dev) {
        return pollfd{dev.native_handle(), POLLIN, 0};
    }

    auto get_pollfds(auto const& devs) {
        return devs
               | std::views::transform([](fs8::evdev const& dev) noexcept {
                     return pollfd{dev.native_handle(), POLLIN, 0};
                 })
               | std::ranges::to<std::vector>();
    }

} // namespace

void basic_device_registry::add(evdev&& inp_dev) {
    devs.emplace_back(std::move(inp_dev));
}

void basic_device_registry::add(device_query const& inp_query) {
    queries.emplace_back(std::move(inp_query));
}

context_action basic_device_registry::operator()(start_tag) {
    using enum context_action;

    if (monitor.has_value()) {
        return next;
    }

    monitor = std::make_optional<udev_monitor>();
    fds     = get_pollfds(devices());


    for (auto const& cur_query : queries) {
        if (!cur_query.fail_on_no_match) {
            continue;
        }

        auto const are_matched = std::bind_back(static_cast<bool (*)(evdev const&, device_query const&)>(matches), cur_query);
        if (!std::ranges::any_of(devs, are_matched)) [[unlikely]] {
            log("Needed this query but didn't found it: {}", to_string(cur_query));
            return exit;
        }
    }
    return next;
}
