// Created by moisrex on 6/22/24.

module;
#include <filesystem>
#include <poll.h>
#include <span>
#include <vector>
export module foresight.mods.intercept;
import foresight.devices.evdev;
import foresight.mods.context;
import foresight.main.utils;

export namespace fs8 {
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
        void set_files(std::string_view);
        void set_files(std::span<std::string_view const>);

        void add_dev(evdev&& dev);
        void add_file(input_file_type const&);
        void add_files(std::string_view);
        void add_files(std::span<std::string_view const>);

        template <std::ranges::range R>
            requires std::convertible_to<std::ranges::range_value_t<R>, evdev&&>
        void add_devs(R&& inp_devs) {
            for (evdev&& dev : std::forward<R>(inp_devs)) {
                add_dev(std::move(dev));
            }
        }

        /// Get a view of the devices, it's const to make sure it's not being modified since the file
        /// descriptors may change, and if they do, we wouldn't know about it.
        [[nodiscard]] std::span<evdev const> devices() const noexcept;

        /// Make sure to re-commit after modification
        /// todo: write a RAII wrapper for it that calls commit on destructor, and return that instead
        [[nodiscard]] std::span<evdev> devices() noexcept;

        /// Apply the changes to devices
        void commit();

        /**
         * Start running the interceptor
         */
        context_action operator()(Context auto& ctx, load_event_tag) noexcept {
            using enum context_action;
            for (;;) {
                // try to get the next event
                if (auto const action = get_next_event(ctx.event()); action != ignore_event) {
                    return action;
                }
                // if it fails, wait for the events to come from input devices
                if (auto const action = wait_for_event(); !action) [[unlikely]] {
                    return action;
                }
            }
        }

      private:
        /// Wait for the next event from devices
        context_action wait_for_event() noexcept;

        /// Returns `next` if we have one, otherwise it'll return `ignore_event` or `idle`
        context_action get_next_event(event_type& event) noexcept;

        std::vector<evdev>  devs;
        std::vector<pollfd> fds;

        // The index of the current device being processed:
        std::uint16_t index = 0;
    } intercept;

} // namespace fs8
