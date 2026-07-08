// Created by moisrex on 12/16/25.

module;
#include <libudev.h>
#include <string_view>
export module foresight.devices.udev;

namespace fs8 {

    /**
     * This struct manages udev context.
     */
    export struct [[nodiscard]] udev {
        udev() noexcept;
        udev(udev const&) noexcept;
        udev& operator=(udev const&) noexcept;
        udev(udev&&) noexcept;
        udev& operator=(udev&&) noexcept;
        ~udev();

        [[nodiscard]] bool is_valid() const noexcept;

        /// is_valid
        [[nodiscard]] explicit operator bool() const noexcept;

        [[nodiscard]] ::udev* native() const noexcept;

        static udev instance();

      private:
        ::udev* handle;
    };

    /**
     * Represents a `udev` device.
     */
    struct [[nodiscard]] udev_device {
        udev_device() noexcept = default;

        explicit udev_device(::udev_device* device) noexcept : dev{device} {}

        udev_device(::udev*, char const*) noexcept;
        udev_device(udev_device const&) noexcept            = delete;
        udev_device& operator=(udev_device const&) noexcept = delete;
        udev_device(udev_device&&) noexcept                 = default;
        udev_device& operator=(udev_device&&) noexcept      = default;
        ~udev_device() noexcept;

        [[nodiscard]] bool is_valid() const noexcept;

        /// is_valid
        [[nodiscard]] explicit operator bool() const noexcept;

        udev_device parent() const noexcept;
        udev_device parent(char const* subsystem, char const* devtype = nullptr) const noexcept;

        [[nodiscard]] std::string_view subsystem() const noexcept;
        [[nodiscard]] std::string_view devtype() const noexcept;
        [[nodiscard]] std::string_view syspath() const noexcept;
        [[nodiscard]] std::string_view sysname() const noexcept;
        [[nodiscard]] std::string_view sysnum() const noexcept;
        [[nodiscard]] std::string_view devnode() const noexcept;
        [[nodiscard]] std::string_view property(char const*) const noexcept;
        [[nodiscard]] std::string_view driver() const noexcept;
        [[nodiscard]] std::string_view action() const noexcept;
        [[nodiscard]] std::string_view sysattr(char const*) const noexcept;
        [[nodiscard]] bool             has_tag(char const*) const noexcept;

        [[nodiscard]] ::udev_device* native() const noexcept;

      private:
        ::udev_device* dev;
    };

    /**
     * List the devices
     */
    export struct [[nodiscard]] udev_enumerate {
        explicit udev_enumerate(udev const&) noexcept;
        udev_enumerate() noexcept;
        udev_enumerate(udev_enumerate const&) = delete;
        udev_enumerate(udev_enumerate&&) noexcept;
        udev_enumerate& operator=(udev_enumerate const&) = delete;
        udev_enumerate& operator=(udev_enumerate&&) noexcept;
        ~udev_enumerate() noexcept;

        [[nodiscard]] ::udev_enumerate* native() const noexcept;

        [[nodiscard]] bool is_valid() const noexcept;

        [[nodiscard]] explicit operator bool() const noexcept;

        // we use char const* because std::string_view may not be null terminated.
        udev_enumerate& match_subsystem(char const*) noexcept;
        udev_enumerate& nomatch_subsystem(char const*) noexcept;

        udev_enumerate& match_sysattr(char const* name, char const* value = nullptr) noexcept;
        udev_enumerate& nomatch_sysattr(char const* name, char const* value = nullptr) noexcept;

        udev_enumerate& match_property(char const* name, char const* value = nullptr) noexcept;

        udev_enumerate& match_sysname(char const*) noexcept;
        udev_enumerate& match_tag(char const*) noexcept;

        udev_enumerate& match_parent(udev_device const&) noexcept;

      private:
        ::udev_enumerate* handle = nullptr;
        int               code   = 0;
    };

    /**
     * Monitor udev devices for events
     */
    struct [[nodiscard]] udev_monitor {
        explicit udev_monitor(udev const&) noexcept;
        udev_monitor() noexcept;
        udev_monitor(udev_monitor const&)            = delete;
        udev_monitor& operator=(udev_monitor const&) = delete;
        udev_monitor(udev_monitor&&) noexcept;
        udev_monitor& operator=(udev_monitor&&) noexcept;
        ~udev_monitor() noexcept;

        void match_device(char const* subsystem, char const* type = nullptr) noexcept;
        void match_tag(char const*) noexcept;

        [[nodiscard]] bool is_valid() const noexcept;

        /// Get the file descriptor so you can manually watch for it.
        [[nodiscard]] int file_descriptor() const noexcept;

        /// Enable watching
        void enable() noexcept;

        /// Get the device that just we have received a new event for
        udev_device next_device() const noexcept;

        // todo: add manually watching in this class for completeness as well
      private:
        ::udev_monitor* mon;
        int             fd   = 0;
        int             code = 0;
    };


} // namespace fs8
