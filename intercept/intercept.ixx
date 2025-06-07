// Created by moisrex on 6/22/24.

module;
#include <filesystem>
#include <span>
#include <vector>
export module foresight.intercept;
import foresight.evdev;

export struct input_file_type {
    std::filesystem::path file;
    bool grab = false;
};

/**
 * Intercept the keyboard and print them into stdout
 */
export struct interceptor {
    explicit interceptor(std::span<input_file_type const> inp_paths);

    /**
     * Set output file descriptor
     */
    void set_output(FILE* inp_out_fd = stdout) noexcept;

    /**
     * Grab the input
     * It's not recommended if you don't know what you're doing.
     */
    void grab_input();

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
    FILE*              out_fd = stdout;
    std::vector<evdev> devs;
};
