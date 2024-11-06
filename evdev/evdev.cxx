// Created by moisrex on 6/18/24.

module;
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fmt/format.h>
#include <libevdev/libevdev.h>
#include <optional>
#include <unistd.h>
module foresight.evdev;

evdev::evdev(std::filesystem::path const& file) {
    file_descriptor = open(file.c_str(), O_RDWR | O_NONBLOCK);

    if (file_descriptor < 0) {
        return;
    }

    dev = libevdev_new();
    if (dev == nullptr) {
        throw std::domain_error("Failed to init libevdev.");
    }

    set_file(file);
}

evdev::~evdev() {
    if (grabbed && dev != nullptr) {
        libevdev_grab(dev, LIBEVDEV_UNGRAB);
    }
    if (dev != nullptr) {
        libevdev_free(dev);
    }
    if (file_descriptor >= 0) {
        close(file_descriptor);
    }
}

void evdev::set_file(std::filesystem::path const& file) {
    if (file_descriptor >= 0) {
        close(file_descriptor);
        file_descriptor = -1;
    }

    auto const new_fd = open(file.c_str(), O_RDWR | O_NONBLOCK);
    if (new_fd < 0) {
        throw std::system_error();
    }
    set_file(new_fd);
}

void evdev::set_file(int const file) {
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
    file_descriptor = file;

    if (int const res_rc = libevdev_set_fd(dev, file_descriptor); res_rc < 0) {
        close(file_descriptor);
        file_descriptor = -1;
        throw std::domain_error(
          fmt::format("Failed to set file for libevdev ({})\n", std::strerror(-res_rc)));
    }
}

int evdev::native_handle() const noexcept {
    return file_descriptor;
}

libevdev* evdev::device_ptr() noexcept {
    return dev;
}

void evdev::grab_input() {
    if (libevdev_grab(dev, LIBEVDEV_GRAB) < 0) {
        throw std::domain_error("Grabbing the input failed.");
    }
    grabbed = true;
}

void evdev::stop(bool const should_stop) {
    is_stopped = should_stop;
}

std::string_view evdev::device_name() const noexcept {
    return libevdev_get_name(dev);
}

std::optional<input_event> evdev::next(bool const sync) noexcept {
    input_event input;

    for (;;) {
        switch (libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &input)) {
            case LIBEVDEV_READ_STATUS_SYNC:
            case -EAGAIN: break;
            case LIBEVDEV_READ_STATUS_SUCCESS: {
                return input;
            }
            default: return std::nullopt;
        }

        if (sync || !is_stopped) {
            break;
        }
    }
    return std::nullopt;
}
