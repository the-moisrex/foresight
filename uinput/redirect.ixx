// Created by moisrex on 6/25/24.

module;
#include <atomic>
#include <filesystem>
#include <vector>
export module foresight.redirect;
export import foresight.uinput;

/**
 * Redirect the already input events into the specified virtual device
 */
export struct redirector {
    redirector() = default;

    explicit redirector(uinput&& inp_dev) : dev{std::move(inp_dev)} {}

    /**
     * Set output file descriptor
     */
    void set_input(FILE* inp_in_fd = stdin);

    /**
     * Start running the interceptor
     * @return the exit code
     */
    int loop();

    /**
     * Set the output virtual device
     */
    void set_output(uinput&& inp_dev);

    /**
     * Stop the loop
     */
    void stop();

  private:
    FILE*            inp_fd = stdin;
    uinput           dev{};
    std::atomic_bool started = false;
};
