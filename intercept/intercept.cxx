// Created by moisrex on 6/22/24.

module;
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <poll.h>
#include <ranges>
#include <span>
#include <thread>
#include <vector>
module foresight.intercept;

using foresight::interceptor;

interceptor::interceptor(std::span<std::filesystem::path const> const inp_paths) {
    // convert to `evdev`s.
    for (auto const& file : inp_paths) {
        devs.emplace_back(file);
    }

    // default buffer
    set_output(out_fd);
}

interceptor::interceptor(std::span<input_file_type const> inp_paths) {
    // convert to `evdev`s.
    for (auto const& [file, grab] : inp_paths) {
        auto& dev = devs.emplace_back(file);
        if (grab) {
            dev.grab_input();
        }
    }

    // default buffer
    set_output(out_fd);
}

void interceptor::set_output(int const inp_out_fd) noexcept {
    out_fd = inp_out_fd;

    // disable output buffer
    // std::setbuf(out_fd, nullptr);
}

int interceptor::loop() {
    auto const fd_iter = devs | std::views::transform([](evdev const& dev) noexcept {
                             return pollfd{dev.native_handle(), POLLIN, 0};
                         });

    std::vector<pollfd> fds{fd_iter.begin(), fd_iter.end()};

    for (; !is_stopped;) {
        auto const ret = poll(fds.data(), fds.size(), 100);
        if (ret <= 0) {
            // ignore errors
            std::this_thread::yield();
            continue;
        }

        for (std::size_t index = 0; index != fds.size(); ++index) {
            auto&       dev = devs[index];
            auto const& fd  = fds[index];
            if ((fd.revents & POLLIN) == 0) {
                continue;
            }

            if (is_stopped) {
                break;
            }
            auto const input = dev.next(true);

            if (!input) {
                continue;
            }

            // write to the output, and retry if it doesn't work:
            while (write(out_fd, &input.value(), sizeof(input_event)) != sizeof(input_event)) [[unlikely]] {
                std::this_thread::yield();
                if (is_stopped) {
                    break; // we're done
                }
            }
        }
    }
    return EXIT_SUCCESS;
}

void interceptor::stop(bool const should_stop) {
    is_stopped = should_stop;
}
