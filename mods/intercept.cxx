// Created by moisrex on 6/22/24.

module;
#include <filesystem>
#include <format>
#include <poll.h>
#include <ranges>
#include <span>
#include <vector>
module foresight.mods.intercept;
import foresight.main.log;

using foresight::basic_interceptor;

namespace {

    pollfd get_pollfd(foresight::evdev const& dev) {
        return pollfd{dev.native_handle(), POLLIN, 0};
    }

    auto get_pollfds(std::span<foresight::evdev const> devs) {
        auto const fd_iter = devs | std::views::transform([](foresight::evdev const& dev) noexcept {
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

basic_interceptor::basic_interceptor(std::vector<evdev>&& inp_devs)
  : devs{std::move(inp_devs)},
    fds{get_pollfds(devs)} {}

void basic_interceptor::set_files(std::span<std::filesystem::path const> const inp_paths) {
    // convert to `evdev`s.
    devs.clear();
    fds.clear();
    devs.reserve(inp_paths.size());
    for (auto const& file : inp_paths) {
        auto& dev = devs.emplace_back(file);
        if (!dev.ok()) [[unlikely]] {
            throw std::runtime_error(std::format("Failed to initialize event device {}", file.string()));
        }
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
        if (dev.ok() && grab) {
            dev.grab_input();
        }
        if (!dev.ok()) [[unlikely]] {
            throw std::runtime_error(std::format("Failed to initialize event device {}", file.string()));
        }
    }
    fds = get_pollfds(devs);
}

void basic_interceptor::add_file(input_file_type const inp_path) {
    auto const& [file, grab] = inp_path;
    auto& dev                = devs.emplace_back(file);
    if (dev.ok() && grab) {
        dev.grab_input();
    }
    if (!dev.ok()) [[unlikely]] {
        throw std::runtime_error(std::format("Failed to initialize event device {}", file.string()));
    }
    fds.emplace_back(get_pollfd(dev));
}

void basic_interceptor::add_dev(evdev&& inp_dev) {
    auto& dev = devs.emplace_back(std::move(inp_dev));
    if (!dev.ok()) [[unlikely]] {
        throw std::runtime_error(std::format("Failed to initialize event device {}", dev.device_name()));
    }
    log("Intercepting: '{}' {}", dev.device_name(), dev.physical_location());
    fds.emplace_back(get_pollfd(dev));
}

void basic_interceptor::add_files(std::string_view const query_all) {
    for (auto [match, dev] : foresight::devices(query_all)) {
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
        for (auto [match, dev] : foresight::devices(query)) {
            add_dev(std::move(dev));
        }
    }
}

void basic_interceptor::set_files(std::span<std::string_view const> const query_all) {
    devs.clear();
    fds.clear();
    add_files(query_all);
}

std::span<foresight::evdev const> basic_interceptor::devices() const noexcept {
    return std::span{devs.begin(), devs.end()};
}

std::span<foresight::evdev> basic_interceptor::devices() noexcept {
    return std::span{devs.begin(), devs.end()};
}

void basic_interceptor::commit() {
    fds = get_pollfds(this->devices());
}

foresight::context_action basic_interceptor::operator()(event_type& event) noexcept {
    using enum context_action;

    if (poll(fds.data(), fds.size(), 1000) <= 0) {
        return ignore_event;
    }

    for (std::size_t index = 0; index != fds.size(); ++index) {
        if ((fds[index].revents & POLLIN) == 0) {
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
