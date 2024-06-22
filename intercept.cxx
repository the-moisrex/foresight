// Created by moisrex on 6/22/24.

module;
#include <exception>
#include <filesystem>
module foresight.intercept;

interceptor::interceptor(std::filesystem::path const& inp_path) : dev{inp_path} {
    // disable buffer
    std::setbuf(stdout, nullptr);
}

int interceptor::loop() {
    for (;;) {
        auto const input = dev.next();
        for (std::size_t retry_count = 0; std::fwrite(&input, sizeof(input), 1, stdout) != 1; ++retry_count) {
            if (retry_count == 5) {
                break; // skip this write
            }
        }
    }
}
