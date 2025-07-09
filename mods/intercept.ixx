// Created by moisrex on 6/22/24.

module;
#include <filesystem>
#include <poll.h>
#include <span>
#include <vector>
export module foresight.mods.intercept;
import foresight.evdev;
import foresight.mods.context;
import foresight.mods.caps;

export namespace foresight {
    struct input_file_type {
        std::filesystem::path file;
        bool                  grab = false;
    };

    /**
     * Intercept the keyboard and print them into stdout
     */
    constexpr struct [[nodiscard]] basic_interceptor {
        explicit basic_interceptor(std::span<std::filesystem::path const> inp_paths);
        explicit basic_interceptor(std::span<input_file_type const> inp_paths);
        explicit basic_interceptor(std::vector<evdev>&& inp_devs);

        constexpr basic_interceptor() noexcept                                    = default;
        consteval basic_interceptor(basic_interceptor const&)                     = default;
        constexpr basic_interceptor(basic_interceptor&&) noexcept                 = default;
        consteval basic_interceptor& operator=(basic_interceptor const&) noexcept = default;
        constexpr basic_interceptor& operator=(basic_interceptor&&) noexcept      = default;
        constexpr ~basic_interceptor() noexcept                                   = default;

        void set_files(std::span<std::filesystem::path const>);
        void set_files(std::span<input_file_type const>);

        /**
         * Start running the interceptor
         */
        context_action operator()(event_type& event) noexcept;

      private:
        std::vector<evdev>  devs;
        std::vector<pollfd> fds;
    } intercept;

} // namespace foresight
