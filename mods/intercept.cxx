// Created by moisrex on 6/22/24.

module;
#include <filesystem>
#include <poll.h>
#include <ranges>
#include <span>
#include <vector>
module foresight.mods.intercept;

using foresight::basic_interceptor;

namespace {
    auto get_pollfds(std::span<foresight::evdev const> devs) {
        auto const fd_iter = devs | std::views::transform([](foresight::evdev const& dev) noexcept {
                                 return pollfd{dev.native_handle(), POLLIN, 0};
                             });
        return std::vector<pollfd>{fd_iter.begin(), fd_iter.end()};
    }
} // namespace

constexpr basic_interceptor::basic_interceptor(std::span<std::filesystem::path const> const inp_paths) {
    set_files(inp_paths);
}

constexpr basic_interceptor::basic_interceptor(std::span<input_file_type const> const inp_paths) {
    set_files(inp_paths);
}

constexpr basic_interceptor::basic_interceptor(std::vector<evdev>&& inp_devs)
  : devs{std::move(inp_devs)},
    fds{get_pollfds(devs)} {}

void basic_interceptor::set_files(std::span<std::filesystem::path const> const inp_paths) {
    // convert to `evdev`s.
    devs.clear();
    fds.clear();
    devs.reserve(inp_paths.size());
    for (auto const& file : inp_paths) {
        devs.emplace_back(file);
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
        if (grab) {
            dev.grab_input();
        }
    }
    fds = get_pollfds(devs);
}
