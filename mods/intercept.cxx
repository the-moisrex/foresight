// Created by moisrex on 6/22/24.

module;
#include <cassert>
#include <cstring>
#include <filesystem>
#include <format>
#include <poll.h>
#include <ranges>
#include <span>
#include <vector>
module foresight.mods.intercept;
import foresight.main.log;

using fs8::basic_interceptor;

namespace {

    pollfd get_pollfd(fs8::evdev const& dev) {
        return pollfd{dev.native_handle(), POLLIN, 0};
    }

    auto get_pollfds(std::span<fs8::evdev const> devs) {
        auto const fd_iter = devs | std::views::transform([](fs8::evdev const& dev) noexcept {
                                 return pollfd{dev.native_handle(), POLLIN, 0};
                             });
        return std::vector<pollfd>{fd_iter.begin(), fd_iter.end()};
    }

} // namespace

basic_interceptor::basic_interceptor(std::span<std::filesystem::path const> const inp_paths) {
    set_files(inp_paths);
}

basic_interceptor::basic_interceptor(std::span<input_file_type const> const inp_paths) {
    set_files(inp_paths);
}

basic_interceptor::basic_interceptor(std::vector<evdev>&& inp_devs) : devs{std::move(inp_devs)}, fds{get_pollfds(devs)} {}

void basic_interceptor::set_files(std::span<std::filesystem::path const> const inp_paths) {
    // convert to `evdev`s.
    devs.clear();
    fds.clear();
    devs.reserve(inp_paths.size());
    for (auto const& file : inp_paths) {
        auto& dev = devs.emplace_back(file);
        if (!dev.ok()) [[unlikely]] {
            throw std::runtime_error(std::format("Failed to initialize event device ({}) while setting multiple files with error({}).",
                                                 file.string(),
                                                 to_string(dev.get_status())));
        }
        devs_descs.emplace_back(file.generic_string(), false);
    }
    fds = get_pollfds(devs);
}

void basic_interceptor::set_files(std::span<input_file_type const> const inp_paths) {
    devs.clear();
    fds.clear();
    devs.reserve(inp_paths.size());
    // convert to `evdev`s.
    for (auto const& [file, grab] : inp_paths) {
        auto& dev = devs.emplace_back(file);
        if (!dev.ok()) [[unlikely]] {
            throw std::runtime_error(std::format(
              "Failed to initialize event device ({}) while trying to initialize multiple files with error "
              "({}).",
              file.string(),
              to_string(dev.get_status())));
        }
        dev.grab_input(grab);
        devs_descs.emplace_back(file.generic_string(), grab);
    }
    fds = get_pollfds(devs);
}

void basic_interceptor::add_file(input_file_type const& inp_path) {
    auto const& [file, grab] = inp_path;
    auto& dev                = devs.emplace_back(file);
    if (!dev.ok()) [[unlikely]] {
        throw std::runtime_error(
          std::format("Failed to initialize event device ({}) with error({}).", file.string(), to_string(dev.get_status())));
    }
    dev.grab_input(grab);
    fds.emplace_back(get_pollfd(dev));
    devs_descs.emplace_back(inp_path.file.generic_string(), grab);
}

void basic_interceptor::add_dev(evdev&& inp_dev) {
    auto&      dev  = devs.emplace_back(std::move(inp_dev));
    auto const name = dev.device_name();
    if (!dev.ok()) [[unlikely]] {
        throw std::runtime_error(std::format(
          "Failed to initialize event device ({}) while trying to start intercepting it with error ({}).",
          name,
          to_string(dev.get_status())));
    }
    log("Intercepting: '{}' {}", name, dev.physical_location());
    fds.emplace_back(get_pollfd(dev));
    devs_descs.emplace_back(std::string{name.data(), name.size()}, false);
}

void basic_interceptor::add_files(std::string_view const query_all) {
    for (evdev&& dev : find_devices(query_all)) {
        add_dev(std::move(dev));
    }
}

void basic_interceptor::set_files(std::string_view const query_all) {
    devs.clear();
    fds.clear();
    add_files(query_all);
}

void basic_interceptor::add_files(std::span<std::string_view const> const query_all) {
    for (auto const query : query_all) {
        for (evdev&& dev : find_devices(query)) {
            add_dev(std::move(dev));
        }
    }
}

void basic_interceptor::set_files(std::span<std::string_view const> const query_all) {
    devs.clear();
    fds.clear();
    add_files(query_all);
}

std::span<fs8::evdev const> basic_interceptor::devices() const noexcept {
    return std::span{devs.begin(), devs.end()};
}

std::span<fs8::evdev> basic_interceptor::devices() noexcept {
    return std::span{devs.begin(), devs.end()};
}

void basic_interceptor::commit() {
    fds = get_pollfds(this->devices());
}

void basic_interceptor::retry_failed_devices() noexcept {
    if (devs_descs.size() == devs.size()) [[likely]] {
        return;
    }

    auto const now = std::chrono::steady_clock::now();

    // don't retry too soon
    if ((now - last_retry) < retry_period) {
        return;
    }
    last_retry = now;

    for (auto const& desc : devs_descs) {
        bool found = false;
        for (auto const& dev : devs) {
            if (dev.device_name() == desc.name) {
                // We already have this device
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }

        // found a removed device, try finding it again:
        auto dev = device(desc.name);
        if (!only_matching(dev) || !only_ok(dev)) {
            log("Failed to reconnect to {}, tried {}", desc.name, dev.dev.device_name());
            // failed to find a working device
            continue;
        }

        // Add the device again if it's okay now
        // todo: check if it's the same device somehow, checking the name is not enough because we might have the location instead of the name of the device
        dev.dev.grab_input(desc.grabbed);
        add_dev(std::move(dev.dev));
    }
}

fs8::context_action basic_interceptor::wait_for_event() noexcept {
    using enum context_action;

    // reset device index
    index = 0;

    // Check for empty containers to avoid undefined behavior
    if (fds.empty() || devs.empty()) [[unlikely]] {
        log("No devices to intercept");
        return exit;
    }

    // Poll file descriptors and handle errors properly
    if (auto const poll_result = poll(fds.data(), fds.size(), retry_period.count()); poll_result <= 0) [[unlikely]] {
        if (poll_result == 0) {
            retry_failed_devices();
            // Timeout occurred
            return ignore_event;
        }
        // Error occurred (could log errno here)
        log("Error: {}", std::strerror(errno));
        return ignore_event;
    }

    return next;
}

fs8::context_action basic_interceptor::get_next_event(event_type& event) noexcept {
    using enum context_action;

    for (; index != fds.size(); ++index) {
        auto const pfd = fds[index];
        if ((pfd.revents & POLLIN) == 0) {
            // Check for errors on this file descriptor
            // todo: give better error messages
            if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) [[unlikely]] {
                // Handle device errors/disconnections here.
                // If any of them get disconnected, we go to idle mode.
                auto const name = devs[index].device_name();
                if (name == invalid_device_name) {
                    log("Device #{} disconnected?", index);
                } else {
                    log("Device #{} {} disconnected?", index, name);
                }

                // Remove the device from watch list
                fds.erase(std::next(fds.begin(), index));
                devs.erase(std::next(devs.begin(), index));

                // Reset the index
                if (index == devs.size()) {
                    index = 0;
                }

                return ignore_event;
            }

            continue;
        }

        auto const input = devs[index].next();
        if (!input) {
            continue;
        }
        event = input.value();
        return next;
    }
    return ignore_event;
}
