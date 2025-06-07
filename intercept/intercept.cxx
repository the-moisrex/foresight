// Created by moisrex on 6/22/24.

module;
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <linux/input.h>
#include <span>
#include <thread>
module foresight.intercept;

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

void interceptor::set_output(FILE* inp_out_fd) noexcept {
    out_fd = inp_out_fd;

    // disable output buffer
    std::setbuf(out_fd, nullptr);
}

int interceptor::loop() {
    for (;;) {
        for (auto& dev : devs) {
            if (dev.is_done()) {
                return EXIT_SUCCESS;
            }

            auto const input = dev.next(true);

            if (!input) {
                if (dev.is_done()) {
                    return EXIT_SUCCESS;
                }
                continue;
            }

            // write to the output, and retry if it doesn't work:
            while (std::fwrite(&input.value(), sizeof(input_event), 1, out_fd) != 1) {
                std::this_thread::yield();
                if (dev.is_done()) {
                    break; // skip this write
                }
            }
        }
    }
    return EXIT_SUCCESS;
}

void interceptor::stop(bool const should_stop) {
    for (auto& dev : devs) {
        dev.stop(should_stop);
    }
}
