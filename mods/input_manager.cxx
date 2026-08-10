// Created by moisrex on 7/18/26.

module;
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <generator>
#include <list>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
module fs8.mods.input_manager;
import fs8.devices.evdev;
import fs8.devices.udev;
import fs8.devices.queries;
import fs8.mods.io_manager;
import fs8.context;
import fs8.log;

using fs8::basic_input_manager;
using fs8::context_action;
using fs8::io_event;
using fs8::io_fd;

namespace {

    /// A udev add/bind/change notification can arrive before the device node is
    /// fully set up (e.g. udev still applying group permissions), so retry a
    /// bounded number of times before giving up on a device.
    [[nodiscard]] fs8::evdev open_device(fs8::device_query const& query, fs8::udev_device const& dev, int const retries = 15) {
        if (dev.devnode().empty()) [[unlikely]] {
            return fs8::initialize(query, dev); // no node; retrying would be pointless
        }
        for (int attempt = 0; attempt <= retries; ++attempt) {
            auto edev = fs8::initialize(query, dev);
            if (edev.is_ok() || edev.get_status() != fs8::evdev_status::failed_to_open_file || attempt == retries) {
                return edev;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return fs8::initialize(query, dev);
    }

} // namespace

template <>
struct fs8::pimpl_idiom<basic_input_manager>::impl {
    bool                     started = false;
    udev_monitor             monitor;
    std::list<evdev>         devs; // stable handles; todo: switch to std::hive once available
    std::vector<std::string> syspaths;
    std::vector<device_query> queries;
    std::vector<bool>         query_matched;

    [[nodiscard]] bool has_syspath(std::string_view const path) const noexcept {
        return std::ranges::any_of(syspaths, [&](std::string const& cur) noexcept {
            return cur == path;
        });
    }

    void erase_by_syspath(std::string_view const path) {
        for (std::size_t i = 0; i < syspaths.size(); ++i) {
            if (syspaths[i] != path) {
                continue;
            }
            devs.erase(std::next(devs.begin(), static_cast<std::ptrdiff_t>(i)));
            syspaths.erase(syspaths.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }

    void add_udev_device(udev_device&& event_dev) {
        if (!event_dev) [[unlikely]] {
            return;
        }

        auto const action = event_dev.action();
        auto const path   = event_dev.syspath();
        if (path.empty()) [[unlikely]] {
            return;
        }

        log("DEBUG add_udev_device: action={} syspath={} sysname={} subsystem={} ID_INPUT={} ID_INPUT_KEYBOARD={}",
            action,
            path,
            event_dev.sysname(),
            event_dev.subsystem(),
            event_dev.property("ID_INPUT"),
            event_dev.property("ID_INPUT_KEYBOARD"));

        if (action == "remove" || action == "unbind") {
            erase_by_syspath(path);
            return;
        }

        if (action != "add" && action != "bind" && action != "change") {
            return;
        }

        if (has_syspath(path)) {
            return; // already tracked; do not duplicate
        }

        for (auto const& cur_query : queries) {
            log("DEBUG query count={} matching {}", queries.size(), to_string(cur_query));
            if (!matches(event_dev, cur_query)) {
                continue;
            }
            auto edev = open_device(cur_query, event_dev);
            if (!edev.is_ok()) {
                log("Device '{}' status: {}", path, to_string(edev.get_status()));
                continue;
            }
            devs.emplace_back(std::move(edev));
            syspaths.emplace_back(path);
            return; // first matching query wins
        }
    }

    void drain() {
        while (auto event_dev = monitor.next_device()) {
            add_udev_device(std::move(event_dev));
        }
    }

    void enumerate() {
        udev_enumerate enumerator{};
        if (!enumerator) [[unlikely]] {
            return;
        }

        for (auto const& cur_query : queries) {
            match(enumerator, cur_query);
        }
        enumerator.scan_devices();

        query_matched.assign(queries.size(), false);
        for (std::size_t i = 0; i < queries.size(); ++i) {
            for (auto event_dev : filter_devices(enumerator, queries[i])) {
                auto const path = event_dev.syspath();
                if (path.empty() || has_syspath(path)) {
                    continue;
                }
                auto edev = initialize(queries[i], event_dev);
                if (!edev.is_ok()) {
                    log("Device '{}' status: {}", path, to_string(edev.get_status()));
                    continue;
                }
                devs.emplace_back(std::move(edev));
                syspaths.emplace_back(path);
                query_matched[i] = true;
            }
        }
    }
};

void basic_input_manager::add(evdev&& inp_dev) {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->devs.emplace_back(std::move(inp_dev));
    pimpl->syspaths.emplace_back();
}

void basic_input_manager::add(device_query const& inp_query) {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->queries.emplace_back(inp_query);
}

std::ranges::subrange<std::list<fs8::evdev>::const_iterator> basic_input_manager::devices() const noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return {};
    }
    return std::ranges::subrange(pimpl->devs.begin(), pimpl->devs.end());
}

std::ranges::subrange<std::list<fs8::evdev>::iterator> basic_input_manager::devices() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return {};
    }
    return std::ranges::subrange(pimpl->devs.begin(), pimpl->devs.end());
}

context_action basic_input_manager::operator()(io_fd const& ready_fd) noexcept {
    using enum context_action;
    if (pimpl.get() == nullptr) [[unlikely]] {
        return next;
    }
    // We only ever register the udev monitor FD; anything else is unexpected
    // and must be ignored safely.
    if (ready_fd.fd != pimpl->monitor.file_descriptor()) [[unlikely]] {
        return next;
    }
    try {
        pimpl->drain();
    } catch (...) {
        // Hotplug handling must never take the pipeline down.
    }
    return next;
}

context_action basic_input_manager::start(basic_io_manager& io) noexcept try {
    using enum context_action;

    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }

    if (!pimpl->started) {
        pimpl->started = true;

        if (!pimpl->monitor.is_valid()) [[unlikely]] {
            log("Cannot start monitoring.");
            return exit;
        }

        for (auto const& cur_query : pimpl->queries) {
            match(pimpl->monitor, cur_query);
        }
        pimpl->monitor.enable();

        pimpl->enumerate();

        // `fail_on_no_match` applies during startup; a runtime disconnection
        // waits for hotplug reconnection instead of failing.
        for (std::size_t i = 0; i < pimpl->queries.size(); ++i) {
            auto const& cur_query = pimpl->queries[i];
            if (!cur_query.fail_on_no_match) {
                continue;
            }
            if (i >= pimpl->query_matched.size() || !pimpl->query_matched[i]) [[unlikely]] {
                log("Needed this query but didn't found it: {}", to_string(cur_query));
                return exit;
            }
        }
    }

    // Re-register the monitor FD after restarts too; `io_manager` clears all
    // registrations on `start`, and `watch` replaces in place if already there.
    if (!io.watch(io_fd{.fd = pimpl->monitor.file_descriptor(), .events = io_event::in}, *this)) [[unlikely]] {
        log("Cannot register the udev monitor.");
        return exit;
    }

    return next;
} catch (...) {
    return context_action::idle;
}
