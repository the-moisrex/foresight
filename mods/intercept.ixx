// Created by moisrex on 6/22/24.

module;
#include <filesystem>
#include <poll.h>
#include <ranges>
#include <span>
#include <vector>
export module foresight.mods.intercept;
import foresight.evdev;
import foresight.mods.context;

export namespace foresight {
    struct input_file_type {
        std::filesystem::path file;
        bool                  grab = false;
    };

    /**
     * Intercept the keyboard and print them into stdout
     */
    constexpr struct basic_interceptor {
        constexpr explicit basic_interceptor(std::span<std::filesystem::path const> inp_paths);
        constexpr explicit basic_interceptor(std::span<input_file_type const> inp_paths);
        constexpr explicit basic_interceptor(std::vector<evdev>&& inp_devs);

        constexpr basic_interceptor() noexcept                                    = default;
        consteval basic_interceptor(basic_interceptor const&)                     = default;
        constexpr basic_interceptor(basic_interceptor&&) noexcept                 = default;
        consteval basic_interceptor& operator=(basic_interceptor const&) noexcept = default;
        constexpr basic_interceptor& operator=(basic_interceptor&&) noexcept      = default;
        constexpr ~basic_interceptor() noexcept                                   = default;

        void set_files(std::span<std::filesystem::path const>);
        void set_files(std::span<input_file_type const>);

        template <typename... Args>
        consteval basic_interceptor operator()(Args&&... args) {
            return basic_interceptor{std::forward<Args>(args)...};
        }

        /**
         * Start running the interceptor
         */
        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;

            if (poll(fds.data(), fds.size(), 1000) <= 0) {
                return ignore_event;
            }

            for (std::size_t index = 0; index != fds.size(); ++index) {
                auto&       dev    = devs[index];
                auto const& cur_fd = fds[index];
                if ((cur_fd.revents & POLLIN) == 0) {
                    continue;
                }

                auto const input = dev.next(true);
                if (!input) {
                    continue;
                }
                ctx.event(input.value());
                return next;
            }
            return ignore_event;
        }

      private:
        std::vector<evdev>  devs;
        std::vector<pollfd> fds;
    } intercept;

} // namespace foresight
