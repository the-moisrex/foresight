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

    [[nodiscard]] pollfd make_pollfd(int fd, short events = POLLIN) noexcept {
        return pollfd{.fd = fd, .events = events, .revents = 0};
    }

    [[nodiscard]] pollfd get_pollfd(fs8::evdev const& dev) noexcept {
        return pollfd{dev.native_handle(), POLLIN, 0};
    }

    [[nodiscard]] auto get_pollfds(auto const& devs) {
        return devs | std::views::transform(get_pollfd) | std::ranges::to<std::vector>();
    }

    void rebuild_pollfds(std::vector<pollfd>& fds, auto const& devs, fs8::udev_monitor* mon) {
        fds = get_pollfds(devs);
        if (mon != nullptr && mon->is_valid()) {
            fds.push_back(pollfd{mon->file_descriptor(), POLLIN, 0});
        }
    }
} // namespace

template <>
struct fs8::pimpl_idiom<basic_device_registry>::impl {
    udev_monitor              monitor;
    std::vector<evdev>        devs;
    std::vector<device_query> queries;
    std::vector<pollfd>       fds;

    void rebuild_fds() noexcept {
        fds.clear();
        fds.push_back(make_pollfd(monitor.file_descriptor()));
        for (auto const& dev : devs) {
            if (dev.is_ok()) {
                fds.push_back(get_pollfd(dev));
            }
        }
    }

    void handle_udev_event(udev_device&& event_dev) noexcept {
        if (!event_dev) [[unlikely]] {
            return;
        }

        auto const action = event_dev.action();
        auto const path   = event_dev.syspath();

        if (action == "remove" || action == "unbind") {
            // Remove any matching device from our list.
            std::erase_if(devs, [&](evdev const& d) {
                // Compare by syspath or by the underlying file descriptor / node.
                // The exact comparison depends on how evdev stores the path;
                return d.physical_location() == path;
            });
            rebuild_fds();
            return;
        }

        if (action == "add" || action == "bind" || action == "change") {
            // Does any registered query match this new device?
            for (auto const& cur_query : queries) {
                if (!matches(event_dev, cur_query)) {
                    continue;
                }
                auto edev = initialize(cur_query, event_dev);
                if (!edev.is_ok()) {
                    log("Device '{}' status: {}", event_dev.syspath(), to_string(edev.get_status()));
                    continue;
                }

                devs.emplace_back(std::move(edev));
                rebuild_fds();
                break; // first matching query wins
            }
        }
    }

    void update() {
        // Non-blocking poll of the monitor + all device FDs.
        // (The surrounding context system is expected to call us frequently.)
        if (fds.empty()) {
            return;
        }

        int const ready = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), 0);
        if (ready <= 0) {
            return;
        }

        // Index 0 is always the udev monitor.
        if (fds[0].revents & (POLLIN | POLLPRI)) {
            while (true) {
                auto event = monitor.next_device();
                if (!event) {
                    break;
                }
                handle_udev_event(std::move(event));
            }
        }

        // Device FDs (indices 1..) – just clear the revents; the actual event
        // reading is left to the higher-level input processing code that owns
        // the evdev objects.
        for (std::size_t i = 1; i < fds.size(); ++i) {
            fds[i].revents = 0;
        }
    }
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

context_action basic_device_registry::operator()(start_tag) noexcept try {
    using enum context_action;

    if (pimpl.get() != nullptr) {
        return next;
    }

    init_impl();

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
    pimpl->rebuild_fds();

    return next;
} catch (...) {
    return context_action::idle;
}

// context_action basic_device_registry::operator()(update_tag) {
//     using enum context_action;
//     pimpl->update();
//     return next;
// }
