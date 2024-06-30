// Created by moisrex on 6/25/24.

module;
#include <filesystem>
#include <span>
#include <vector>
#include <atomic>
export module foresight.redirect;
export import foresight.uinput;

/**
 * Intercept the keyboard and print them into stdout
 */
export struct redirector {

    struct device_pending {
        uinput dev;
        std::size_t pending = 0;
    };

    redirector() = default;

    /**
     * Set output file descriptor
     */
    void set_input(FILE* inp_in_fd = stdin) noexcept;

    /**
     * Start running the interceptor
     * @return the exit code
     */
    int loop();


    void append(uinput&& dev);

    /**
     * Stop the loop
     */
    void stop(bool should_stop = true);

  private:
    FILE*              inp_fd = stdin;
    std::vector<device_pending> devs;
    std::atomic_bool started = false;
};
