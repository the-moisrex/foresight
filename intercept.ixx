// Created by moisrex on 6/22/24.

module;
#include <filesystem>
export module foresight.intercept;
import foresight.evdev;

/**
 * Intercept the keyboard and print them into stdout
 */
export struct interceptor {
    explicit interceptor(std::filesystem::path const& inp_path);

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

  private:
    FILE* out_fd = stdout;
    evdev dev;
};
