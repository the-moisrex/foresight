// Created by moisrex on 6/18/24.

module;
#include <filesystem>
#include <libevdev/libevdev.h>
#include <optional>
#include <string_view>
#include <utility>
export module foresight.evdev;

namespace foresight {
    /**
     * This is a wrapper for libevdev's related features
     */
    export struct evdev {
        explicit evdev(std::filesystem::path const& file);
        consteval evdev()                                  = default;
        consteval evdev(evdev const&) noexcept             = default;
        constexpr evdev(evdev&& inp) noexcept              = default;
        constexpr evdev& operator=(evdev&& other) noexcept = default;
        consteval evdev& operator=(evdev const&) noexcept  = default;
        ~evdev();

        // evdev(evdev const&)            = delete;
        // evdev& operator=(evdev const&) = delete;


        /// change the input event file (for example /dev/input/eventX)
        void set_file(std::filesystem::path const& file);
        void set_file(int file);

        [[nodiscard]] int       native_handle() const noexcept;
        [[nodiscard]] libevdev* device_ptr() const noexcept;

        /// check if everything is okay
        [[nodiscard]] bool ok() const noexcept {
            return dev != nullptr;
        }

        void grab_input();


        /**
         * Retrieve the device's name, either as set by the caller or as read from
         * the kernel. The string returned is valid until libevdev_free() or until
         * libevdev_set_name(), whichever comes earlier.
         */
        [[nodiscard]] std::string_view device_name() const noexcept;

        /**
         * Get a new input_event form the input device
         */
        [[nodiscard]] std::optional<input_event> next() noexcept;

      private:
        libevdev* dev     = nullptr;
        bool      grabbed = false;
    };
} // namespace foresight
