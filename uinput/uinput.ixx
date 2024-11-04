// Created by moisrex on 6/29/24.

module;
#include <filesystem>
#include <libevdev/libevdev-uinput.h>
#include <system_error>
export module foresight.uinput;
export import foresight.evdev;

/**
 * A virtual device
 */
export class uinput {
  public:
    uinput() noexcept = default;
    uinput(evdev& evdev_dev, std::filesystem::path const& file) noexcept;
    uinput(libevdev const* evdev_dev, std::filesystem::path const& file) noexcept;
    uinput(libevdev const* evdev_dev, int file_descriptor) noexcept;
    uinput(uinput const&)                     = delete;
    uinput(uinput&&) noexcept                 = default;
    uinput& operator=(uinput const&) noexcept = delete;
    uinput& operator=(uinput&&) noexcept      = default;
    ~uinput() noexcept;

    [[nodiscard]] std::error_code error() const noexcept;
    [[nodiscard]] bool            is_ok() const noexcept;

    [[nodiscard]] explicit operator bool() const noexcept {
        return is_ok();
    }

    /**
     * Configure the virtual device
     * @param evdev_dev libevdev device to get the device info from
     * @param file_descriptor file descriptor of the output virtual device
     */
    void set_device(libevdev const* evdev_dev, int file_descriptor) noexcept;

    /**
     * Return the file descriptor used to create this uinput device. This is the
     * fd pointing to /dev/uinput. This file descriptor may be used to write
     * events that are emitted by the uinput device.
     * Closing this file descriptor will destroy the uinput device, you should
     * call libevdev_uinput_destroy() first to free allocated resources.
     *
     * @return The file descriptor used to create this device
     */
    [[nodiscard]] int native_handle() const noexcept;


    /**
     * Return the syspath representing this uinput device. If the UI_GET_SYSNAME
     * ioctl is not available, libevdev makes an educated guess.
     * The UI_GET_SYSNAME ioctl is available since Linux 3.15.
     *
     * The syspath returned is the one of the input node itself
     * (e.g. /sys/devices/virtual/input/input123), not the syspath of the device
     * node returned with libevdev_uinput_get_devnode().
     *
     * @note This function may return empty string if UI_GET_SYSNAME is not available.
     * In that case, libevdev uses ctime and the device name to guess devices.
     * To avoid false positives, wait at least 1.5s between creating devices that
     * have the same name.
     *
     * @note FreeBSD does not have sysfs, on FreeBSD this function always returns
     * empty string.
     *
     * @return The syspath for this device, including the preceding /sys
     *
     * @see devnode()
     */
    [[nodiscard]] std::string_view syspath() const noexcept;

    /**
     * Return the device node representing this uinput device.
     *
     * @note This function may return empty string. libevdev may have to guess the
     * syspath and the device node.
     *
     * @note On FreeBSD, this function can not return empty string. libudev uses the
     * UI_GET_SYSNAME ioctl to get the device node on this platform and if that
     * fails, the call to libevdev_uinput_create_from_device() fails.
     *
     * @return The device node for this device, in the form of /dev/input/eventN
     */
    [[nodiscard]] std::string_view devnode() const noexcept;

    bool write(unsigned int type, unsigned int code, int value) noexcept;
    bool write(input_event const& event) noexcept;

  private:
    libevdev_uinput* dev = nullptr;

    // we can use std::expected<..., error_code> instead of this, but we're using C++20
    std::error_code err = std::make_error_code(std::errc::bad_file_descriptor);
};
