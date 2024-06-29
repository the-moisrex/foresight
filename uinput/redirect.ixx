// Created by moisrex on 6/25/24.

module;
#include <filesystem>
#include <span>
#include <vector>
export module foresight.redirect;
import foresight.evdev;

/**
 * Intercept the keyboard and print them into stdout
 */
export struct redirector {
    explicit redirector(std::span<std::filesystem::path const> inp_paths);

    /**
     * Set output file descriptor
     */
    void set_input(FILE* inp_in_fd = stdin) noexcept;

    /**
     * Start running the interceptor
     * @return the exit code
     */
    int loop();

    /**
     * Stop the loop
     */
    void stop(bool should_stop = true);

  private:
    FILE*              inp_fd = stdin;
    std::vector<evdev> devs;
};
