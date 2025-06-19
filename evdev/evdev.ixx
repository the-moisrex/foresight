// Created by moisrex on 6/18/24.

module;
#include <concepts>
#include <filesystem>
#include <libevdev/libevdev.h>
#include <optional>
#include <string_view>
#include <utility>
export module foresight.evdev;
export import foresight.mods.event;

namespace foresight {
    /**
     * This is a wrapper for libevdev's related features
     */
    export struct evdev {
        using ev_type   = event_type::type_type;
        using code_type = event_type::code_type;

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
        void                           device_name(std::string_view) noexcept;

        void enable_event_type(ev_type) noexcept;
        void enable_event_code(ev_type, code_type) noexcept;
        void enable_event_code(ev_type, code_type, void const*) noexcept;

        template <typename... T>
            requires(std::convertible_to<T, code_type> && ...)
        void enable_event_codes(ev_type const type, T const... codes) noexcept {
            (enable_event_code(type, static_cast<code_type>(codes)), ...);
        }

        /**
         * Get a new input_event form the input device
         */
        [[nodiscard]] std::optional<input_event> next() noexcept;

      private:
        libevdev* dev     = nullptr;
        bool      grabbed = false;
    };
} // namespace foresight
