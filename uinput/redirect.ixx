// Created by moisrex on 6/25/24.

module;
#include <atomic>
#include <filesystem>
#include <vector>
export module foresight.redirect;
export import foresight.uinput;

namespace foresight {
    /**
     * Redirect the already input events into the specified virtual device
     */
    export struct redirector {
        redirector() = default;

        template <typename... Args>
        explicit redirector(Args&&... args) : dev{std::forward<Args>(args)...} {}

        explicit redirector(uinput&& inp_dev) : dev{std::move(inp_dev)} {}

        /**
         * Set output file descriptor
         */
        void set_input(int inp_in_fd = STDIN_FILENO);

        /**
         * Start running the interceptor
         * @return the exit code
         */
        int loop();

        /**
         * Set the output virtual device
         */
        void set_output(uinput&& inp_dev);

        template <typename... Args>
        void init_device(Args&&... args) {
            dev = uinput{std::forward<Args>(args)...};
        }

        /**
         * Stop the loop
         */
        void stop();

      private:
        int              inp_fd = STDIN_FILENO;
        uinput           dev{};
        std::atomic_bool started = false;
    };
} // namespace foresight
