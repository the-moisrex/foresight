// Created by moisrex on 7/18/26.

module;
#include <algorithm>
#include <chrono>
#include <functional>
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
    bool                               started = false;
    udev_monitor                       monitor;
    std::list<evdev>                   devs; // stable handles; todo: switch to std::hive once available
    std::vector<query_provider_handle> providers;

    /// Devices are identified by their udev sysname (derived from the fd),
    /// which is the last component of their syspath; only nodes with a devnode
    /// are ever tracked, so the two are equivalent.
    [[nodiscard]] bool has_sysname(std::string_view const name) const noexcept {
        if (name.empty()) [[unlikely]] {
            return false;
        }
        return std::ranges::any_of(devs, [&](evdev const& dev) noexcept {
            return device_sysname(dev) == name;
        });
    }

    void erase_by_sysname(std::string_view const name) {
        if (name.empty()) [[unlikely]] {
            return;
        }
        std::erase_if(devs, [&](evdev const& dev) noexcept {
            return device_sysname(dev) == name;
        });
    }

    void add_udev_device(udev_device&& event_dev) {
        if (!event_dev) [[unlikely]] {
            return;
        }

        auto const action = event_dev.action();
        auto const path   = event_dev.syspath();
        auto const name   = event_dev.sysname();
        if (path.empty()) [[unlikely]] {
            return;
        }

        // log("DEBUG add_udev_device: action={} syspath={} sysname={} subsystem={} ID_INPUT={} ID_INPUT_KEYBOARD={}",
        //     action,
        //     path,
        //     name,
        //     event_dev.subsystem(),
        //     event_dev.property("ID_INPUT"),
        //     event_dev.property("ID_INPUT_KEYBOARD"));

        if (action == "remove" || action == "unbind") {
            erase_by_sysname(name);
            return;
        }

        if (action != "add" && action != "bind" && action != "change") {
            return;
        }

        if (has_sysname(name)) {
            return; // already tracked; do not duplicate
        }

        bool added = false;
        for (auto& provider : providers) {
            if (added) [[unlikely]] {
                break; // first matching query wins
            }
            for (device_query const cur_query : provider()) {
                if (added) [[unlikely]] {
                    break;
                }
                // log("DEBUG matching {}", to_string(cur_query));
                if (!matches(event_dev, cur_query)) {
                    continue;
                }
                auto edev = open_device(cur_query, event_dev);
                if (!edev.is_ok()) {
                    log("Device '{}' status: {}", path, to_string(edev.get_status()));
                    continue;
                }
                devs.emplace_back(std::move(edev));
                added = true;
            }
        }
    }

    void drain() {
        while (auto event_dev = monitor.next_device()) {
            add_udev_device(std::move(event_dev));
        }
    }

    /// Enumerate all queries from every registered provider: match them into
    /// the udev enumerator, then open the devices each query selects. Queries
    /// whose `fail_on_no_match` flag is set and matched nothing are reported
    /// through `on_fail_no_match`.
    void enumerate(std::function_ref<void(device_query const&)> on_fail_no_match) {
        udev_enumerate enumerator{};
        if (!enumerator) [[unlikely]] {
            for (auto& provider : providers) {
                for (device_query const cur_query : provider()) {
                    if (cur_query.fail_on_no_match) {
                        on_fail_no_match(cur_query);
                    }
                }
            }
            return;
        }

        for (auto& provider : providers) {
            for (device_query const cur_query : provider()) {
                match(enumerator, cur_query);
            }
        }
        enumerator.scan_devices();

        for (auto& provider : providers) {
            for (device_query const cur_query : provider()) {
                bool found = false;
                if (!devs.empty()) [[likely]] {
                    for (auto& existing : devs) {
                        if (matches(existing, cur_query)) {
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    found = enumerate_one(cur_query, enumerator);
                }
                if (cur_query.fail_on_no_match && !found) [[unlikely]] {
                    on_fail_no_match(cur_query);
                }
            }
        }
    }

    /// Open devices matching `cur_query` from the enumerated list. Since a
    /// query only constrains how many devices it wants (not which devices it
    /// competes for with other queries), failures here do not consume the
    /// limit: a device rejected at open time simply does not count.
    [[nodiscard]] bool enumerate_one(device_query const& cur_query, udev_enumerate const& enumerator) {
        std::size_t remaining = cur_query.matches_limit == 0 ? 1 : cur_query.matches_limit;
        bool        found     = false;
        for (auto const& entry : enumerator.list_entries()) {
            if (remaining == 0) [[unlikely]] {
                break;
            }
            auto event_dev = udev_device{entry};
            if (!event_dev || event_dev.devnode().empty()) [[unlikely]] {
                continue;
            }
            bool const already_open = has_sysname(event_dev.sysname());
            if (!matches(event_dev, cur_query)) {
                continue;
            }
            if (already_open) {
                continue;
            }
            auto edev = open_device(cur_query, event_dev);
            if (!edev.is_ok()) {
                log("Device '{}' status: {}", event_dev.syspath(), to_string(edev.get_status()));
                continue;
            }
            devs.emplace_back(std::move(edev));
            found = true;
            --remaining;
        }
        return found;
    }
};

void basic_input_manager::add(evdev&& inp_dev) {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->devs.emplace_back(std::move(inp_dev));
}

void basic_input_manager::add_query_provider(query_provider_handle provider) {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    if (provider.identity == nullptr) [[unlikely]] {
        return;
    }
    auto const found = std::ranges::find_if(pimpl->providers, [&](query_provider_handle const& cur) noexcept {
        return cur.identity == provider.identity;
    });
    if (found != pimpl->providers.end()) [[unlikely]] {
        return; // already registered; keep a single handle per provider
    }
    pimpl->providers.push_back(std::move(provider));
}

void basic_input_manager::requery() {
    if (pimpl.get() == nullptr || !pimpl->started) [[unlikely]] {
        return;
    }

    // Rebuild the udev monitor filter so future hotplug events match the
    // fresh set of queries.
    pimpl->monitor.filter_remove();
    for (auto& provider : pimpl->providers) {
        for (device_query const cur_query : provider()) {
            match(pimpl->monitor, cur_query);
        }
    }
    pimpl->monitor.filter_update();

    // Runtime re-enumeration must not fail on no-match; hotplug catches up.
    pimpl->enumerate([](device_query const&) noexcept {});
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

        for (auto& provider : pimpl->providers) {
            for (device_query const cur_query : provider()) {
                match(pimpl->monitor, cur_query);
            }
        }
        pimpl->monitor.enable();

        // `fail_on_no_match` applies during startup; a runtime disconnection
        // waits for hotplug reconnection instead of failing.
        bool failed = false;
        pimpl->enumerate([&](device_query const& cur_query) {
            log("Needed this query but didn't found it: {}", to_string(cur_query));
            failed = true;
        });
        if (failed) [[unlikely]] {
            return exit;
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
