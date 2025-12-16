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
        udev(udev&&) noexcept            = default;
        udev& operator=(udev&&) noexcept = default;
        ~udev();

        [[nodiscard]] bool is_valid() const noexcept;

        /// is_valid
        [[nodiscard]] explicit operator bool() const noexcept;

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

        udev_device(::udev*, std::string_view) noexcept;
        udev_device(udev_device const&) noexcept            = delete;
        udev_device& operator=(udev_device const&) noexcept = delete;
        udev_device(udev_device&&) noexcept                 = default;
        udev_device& operator=(udev_device&&) noexcept      = default;
        ~udev_device() noexcept;

        [[nodiscard]] bool is_valid() const noexcept;

        /// is_valid
        [[nodiscard]] explicit operator bool() const noexcept;

        udev_device parent() const noexcept;
        udev_device parent(std::string_view subsystem, std::string_view devtype = {}) const noexcept;

        [[nodiscard]] std::string_view subsystem() const noexcept;
        [[nodiscard]] std::string_view devtype() const noexcept;
        [[nodiscard]] std::string_view syspath() const noexcept;
        [[nodiscard]] std::string_view sysname() const noexcept;
        [[nodiscard]] std::string_view sysnum() const noexcept;
        [[nodiscard]] std::string_view devnode() const noexcept;
        [[nodiscard]] std::string_view property(std::string_view) const noexcept;
        [[nodiscard]] std::string_view driver() const noexcept;
        [[nodiscard]] std::string_view action() const noexcept;
        [[nodiscard]] std::string_view sysattr(std::string_view) const noexcept;
        [[nodiscard]] bool             has_tag(std::string_view) const noexcept;

      private:
        ::udev_device* dev;
    };


} // namespace fs8
