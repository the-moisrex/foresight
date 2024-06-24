// Created by moisrex on 6/22/24.

module;
#include <cstdlib>
#include <exception>
#include <filesystem>
module foresight.intercept;

interceptor::interceptor(std::filesystem::path const& inp_path) : dev{inp_path} {
    set_output(stdout);
}

void interceptor::set_output(FILE* inp_out_fd) noexcept {
    out_fd = inp_out_fd;
    std::setbuf(out_fd, nullptr);
}

void interceptor::grab_input() {
    dev.grab_input();
}

int interceptor::loop() {
    while (!dev.is_done()) {
        auto const input = dev.next();
        for (std::size_t retry_count = 0; std::fwrite(&input, sizeof(input), 1, out_fd) != 1; ++retry_count) {
            if (dev.is_done() || retry_count == 5) {
                break; // skip this write
            }
        }
    }
    return EXIT_SUCCESS;
}

void interceptor::stop(bool const should_stop) {
    dev.stop(should_stop);
}
