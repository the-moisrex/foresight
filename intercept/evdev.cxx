// Created by moisrex on 6/18/24.

module;
#include <fcntl.h>
#include <filesystem>
#include <libevdev/libevdev.h>
#include <spdlog/spdlog.h>
module foresight.evdev;

evdev::evdev(std::filesystem::path const& file) {
    file_descriptor = open(file.c_str(), O_RDWR | O_NONBLOCK);

    if (file_descriptor < 0) {
        return;
    }


    // init libevdev
    if (int const res_rc = libevdev_new_from_fd(file_descriptor, &dev); res_rc < 0) {
        close(file_descriptor);
        file_descriptor = -1;
        spdlog::critical("Failed to init libevdev ({})\n", strerror(-res_rc));

        // fallback, lets init at least this in order to let the set_file work at least.
        dev = libevdev_new();
    }
}

evdev::~evdev() {
    if (grabbed) {
        libevdev_grab(dev, LIBEVDEV_UNGRAB);
    }
    if (dev != nullptr) {
        libevdev_free(dev);
    }
    if (file_descriptor > 0) {
        close(file_descriptor);
    }
}

void evdev::set_file(std::filesystem::path const& file) {
    if (file_descriptor > 0) {
        close(file_descriptor);
    }

    file_descriptor = open(file.c_str(), O_RDWR | O_NONBLOCK);

    if (file_descriptor < 0) {
        return;
    }

    if (int const res_rc = libevdev_set_fd(dev, file_descriptor); res_rc < 0) {
        close(file_descriptor);
        file_descriptor = -1;
        spdlog::critical("Failed to set file for libevdev ({})\n", strerror(-res_rc));
    }
}

void evdev::grab_input() {
    if (libevdev_grab(dev, LIBEVDEV_GRAB) < 0) {
        spdlog::error("Grabbing the input failed.");
    } else {
        grabbed = true;
    }
}

void evdev::stop(bool const should_stop) {
    is_stopped = should_stop;
}

std::string_view evdev::device_name() const noexcept {
    return libevdev_get_name(dev);
}

std::optional<input_event> evdev::next(bool const sync) {
    input_event input;

    for (;;) {
        switch (int const rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &input)) {
            case LIBEVDEV_READ_STATUS_SYNC:
            case -EAGAIN: break;
            // case LIBEVDEV_READ_STATUS_SYNC:
            //     while (rc == LIBEVDEV_READ_STATUS_SYNC) {
            //         rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &input);
            //     }
            //     break;
            case LIBEVDEV_READ_STATUS_SUCCESS: {
                return input;
            }
            default: {
                spdlog::critical("Failed error from libevdev_next_event ({})\n", rc);
                return std::nullopt;
            }
        }

        if (sync || !is_stopped) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}
