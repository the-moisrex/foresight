// Created by moisrex on 6/18/24.

#include "evdev.h"

#include <libevdev/libevdev.h>
#include <spdlog/spdlog.h>

evdev::evdev(int const file_descriptor) {
    // init libevdev
    if (int const res_rc = libevdev_new_from_fd(file_descriptor, &dev); res_rc < 0) {
        spdlog::critical("Failed to init libevdev ({})\n", strerror(-res_rc));
    }
}

evdev::~evdev() {
    if (dev != nullptr) {
        libevdev_free(dev);
    }
}

std::string_view evdev::device_name() const noexcept {
    return libevdev_get_name(dev);
}
