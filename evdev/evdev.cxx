// Created by moisrex on 6/18/24.

module;
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <libevdev/libevdev.h>
#include <optional>
#include <unistd.h>
module foresight.evdev;

using foresight::evdev;

evdev::evdev(std::filesystem::path const& file) {
    set_file(file);
}

evdev::~evdev() {
    if (grabbed && dev != nullptr) {
        libevdev_grab(dev, LIBEVDEV_UNGRAB);
    }
    auto const file_descriptor = native_handle();
    if (dev != nullptr) {
        libevdev_free(dev);
    }
    if (file_descriptor >= 0) {
        close(file_descriptor);
    }
}

void evdev::set_file(std::filesystem::path const& file) {
    auto const file_descriptor = native_handle();
    if (file_descriptor >= 0) {
        close(file_descriptor);
    }

    auto const new_fd = open(file.c_str(), O_RDWR);
    if (new_fd < 0) {
        throw std::invalid_argument(std::format("Failed to open file '{}'", file.string()));
    }
    set_file(new_fd);
}

void evdev::set_file(int const file) {
    auto const file_descriptor = native_handle();
    if (file_descriptor >= 0) {
        close(file_descriptor);
    }
    if (file < 0) {
        throw std::invalid_argument("The file descriptor specified is not valid.");
    }
    if (dev == nullptr) {
        dev = libevdev_new();
        if (dev == nullptr) {
            throw std::domain_error("Unable to read the code.");
        }
    }

    if (int const res_rc = libevdev_set_fd(dev, file); res_rc < 0) {
        close(file);
        throw std::domain_error(
          std::format("Failed to set file for libevdev ({})\n", std::strerror(-res_rc)));
    }
}

int evdev::native_handle() const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return -1;
    }
    return libevdev_get_fd(dev);
}

libevdev* evdev::device_ptr() const noexcept {
    return dev;
}

void evdev::grab_input() {
    if (libevdev_grab(dev, LIBEVDEV_GRAB) < 0) {
        throw std::domain_error("Grabbing the input failed.");
    }
    grabbed = true;
}

std::string_view evdev::device_name() const noexcept {
    return libevdev_get_name(dev);
}

void evdev::device_name(std::string_view const new_name) noexcept {
    libevdev_set_name(dev, new_name.data());
}

void evdev::enable_event_type(ev_type const type) noexcept {
    libevdev_enable_event_type(dev, type);
}

void evdev::enable_event_code(ev_type const type, code_type const code) noexcept {
    enable_event_code(type, code, nullptr);
}

void evdev::enable_event_code(ev_type const type, code_type const code, void const* const value) noexcept {
    libevdev_enable_event_code(dev, type, code, nullptr);
}

std::optional<input_event> evdev::next() noexcept {
    input_event input;

    switch (libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &input)) {
        [[likely]] case LIBEVDEV_READ_STATUS_SUCCESS: { return input; }
        case LIBEVDEV_READ_STATUS_SYNC:
        case -EAGAIN: break;
        default: return std::nullopt;
    }

    return std::nullopt;
}
