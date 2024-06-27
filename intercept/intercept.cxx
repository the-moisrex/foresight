// Created by moisrex on 6/22/24.

module;
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <linux/input.h>
#include <span>
module foresight.intercept;

interceptor::interceptor(std::span<std::filesystem::path const> inp_paths) {
    // convert to `evdev`s.
    for (auto const& path : inp_paths) {
        devs.emplace_back(path);
    }
    // This gives GCC internal error on G++ 14.1.1 20240522
    // std::ranges::transform(inp_paths, std::back_inserter(devs), [](auto const& path) {
    //     return evdev{path};
    // });

    // default buffer
    set_output(out_fd);
}

void interceptor::set_output(FILE* inp_out_fd) noexcept {
    out_fd = inp_out_fd;

    // disable output buffer
    std::setbuf(out_fd, nullptr);
}

void interceptor::grab_input() {
    for (auto& dev : devs) {
        dev.grab_input();
    }
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
            for (std::size_t retry_count = 0;
                 std::fwrite(&input.value(), sizeof(input_event), 1, out_fd) != 1;
                 ++retry_count)
            {
                if (dev.is_done() || retry_count == 3) {
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
