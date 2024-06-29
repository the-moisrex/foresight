// Created by moisrex on 6/29/24.

module;
#include <libevdev/libevdev-uinput.h>
#include <system_error>
module foresight.uinput;

uinput::uinput(libevdev const* evdev_dev, int const file_descriptor) {
    if (auto const ret = libevdev_uinput_create_from_device(evdev_dev, file_descriptor, &dev); ret != 0) {
        err = std::make_error_code(static_cast<std::errc>(-ret));
    }
}

uinput::uinput(evdev const& evdev_dev) : uinput{evdev_dev.device_ptr(), evdev_dev.native_handle()} {}

uinput::~uinput() {
    if (dev != nullptr) {
        libevdev_uinput_destroy(dev);
        dev = nullptr;
        err.clear();
    }
}

std::error_code uinput::error() const noexcept {
    return err;
}

bool uinput::is_ok() const noexcept {
    return static_cast<bool>(err) && dev != nullptr;
}

int uinput::native_handle() const noexcept {
    if (!is_ok()) {
        return -1;
    }
    return libevdev_uinput_get_fd(dev);
}

std::string_view uinput::syspath() const noexcept {
    if (!is_ok()) {
        return {};
    }
    return libevdev_uinput_get_syspath(dev);
}

std::string_view uinput::devnode() const noexcept {
    if (!is_ok()) {
        return {};
    }
    return libevdev_uinput_get_devnode(dev);
}

bool uinput::write(unsigned int const type, unsigned int const code, int const value) {
    if (auto const ret = libevdev_uinput_write_event(dev, type, code, value); ret != 0) {
        err = std::make_error_code(static_cast<std::errc>(-ret));
        return false;
    }
    err.clear();
    return true;
}

bool uinput::write(input_event const& event) {
    return write(event.type, event.code, event.value);
}
