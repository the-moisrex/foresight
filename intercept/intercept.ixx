// Created by moisrex on 6/22/24.

module;
#include <atomic>
#include <filesystem>
#include <span>
#include <unistd.h>
#include <vector>
export module foresight.intercept;
import foresight.evdev;

export struct input_file_type {
    std::filesystem::path file;
    bool                  grab = false;
};

/**
 * Intercept the keyboard and print them into stdout
 */
export struct interceptor {
    explicit interceptor(std::span<std::filesystem::path const> inp_paths);
    explicit interceptor(std::span<input_file_type const> inp_paths);

    /**
     * Set output file descriptor
     */
    void set_output(int inp_out_fd = STDOUT_FILENO) noexcept;

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
    int                out_fd = STDOUT_FILENO;
    std::atomic_bool is_stopped = false;
    std::vector<evdev> devs;
};
