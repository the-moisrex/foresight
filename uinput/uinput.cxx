// Created by moisrex on 6/29/24.

module;
#include <cassert>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <libevdev/libevdev-uinput.h>
#include <system_error>
module foresight.uinput;
import foresight.mods.event;

using foresight::basic_uinput;

basic_uinput::basic_uinput(evdev& evdev_dev, std::filesystem::path const& file) noexcept
  : basic_uinput(evdev_dev.device_ptr(), file) {}

basic_uinput::basic_uinput(evdev& evdev_dev, int const file_descriptor) noexcept
  : basic_uinput(evdev_dev.device_ptr(), file_descriptor) {}

basic_uinput::basic_uinput(libevdev const* evdev_dev, std::filesystem::path const& file) noexcept {
    auto const file_descriptor = open(file.c_str(), O_RDWR | O_NONBLOCK);
    if (file_descriptor < 0) {
        err_code = static_cast<std::errc>(errno);
        return;
    }
    set_device(evdev_dev, file_descriptor);
}

basic_uinput::basic_uinput(libevdev const* evdev_dev, int const file_descriptor) noexcept {
    set_device(evdev_dev, file_descriptor);
}

std::error_code basic_uinput::error() const noexcept {
    return std::make_error_code(err_code);
}

bool basic_uinput::is_ok() const noexcept {
    return dev != nullptr && err_code == std::errc{};
}

void basic_uinput::set_device(libevdev const* evdev_dev, int const file_descriptor) noexcept {
    dev = nullptr;

    // If uinput_fd is @ref LIBEVDEV_UINPUT_OPEN_MANAGED, libevdev_uinput_create_from_device()
    // will open @c /dev/uinput in read/write mode and manage the file descriptor.
    // Otherwise, uinput_fd must be opened by the caller and opened with the
    // appropriate permissions.
    if (auto const ret = libevdev_uinput_create_from_device(evdev_dev, file_descriptor, &dev); ret != 0) {
        err_code = static_cast<std::errc>(-ret);
    }
}

void basic_uinput::set_device(evdev const& dev, int const file_descriptor) noexcept {
    set_device(dev.device_ptr(), file_descriptor);
}

int basic_uinput::native_handle() const noexcept {
    if (!is_ok()) {
        return -1;
    }
    return libevdev_uinput_get_fd(dev);
}

std::string_view basic_uinput::syspath() const noexcept {
    if (!is_ok()) {
        return {};
    }
    return libevdev_uinput_get_syspath(dev);
}

std::string_view basic_uinput::devnode() const noexcept {
    if (!is_ok()) {
        return {};
    }
    return libevdev_uinput_get_devnode(dev);
}

bool basic_uinput::write(unsigned int const type, unsigned int const code, int const value) noexcept {
    assert(is_ok());
    if (auto const ret = libevdev_uinput_write_event(dev, type, code, value); ret != 0) {
        err_code = static_cast<std::errc>(-ret);
        return false;
    }
    return true;
}

bool basic_uinput::write(input_event const& event) noexcept {
    assert(is_ok());
    return write(event.type, event.code, event.value);
}

bool basic_uinput::write(event_type const& event) noexcept {
    return write(event.native());
}
