// Created by moisrex on 6/18/24.

module;
#include <fcntl.h>
#include <filesystem>
#include <libevdev/libevdev.h>
#include <spdlog/spdlog.h>
module foresight.evdev;

evdev::evdev(std::filesystem::path const& file) {
    file_descriptor = open(file.c_str(), O_RDWR);

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

    file_descriptor = open(file.c_str(), O_RDWR);

    if (file_descriptor < 0) {
        return;
    }

    if (int const res_rc = libevdev_set_fd(dev, file_descriptor); res_rc < 0) {
        close(file_descriptor);
        file_descriptor = -1;
        spdlog::critical("Failed to set file for libevdev ({})\n", strerror(-res_rc));
    }
}

std::string_view evdev::device_name() const noexcept {
    return libevdev_get_name(dev);
}

input_event evdev::next() {
    for (;;) {
        // rc     = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &input);
        input_event input;
        int rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING, &input);

        switch (rc) {
            case -EAGAIN: continue;
            case LIBEVDEV_READ_STATUS_SYNC:
                while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                    rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &input);
                }
                break;
            case LIBEVDEV_READ_STATUS_SUCCESS: {
                break;
            }
            default: {
                spdlog::critical("Failed error from libevdev_next_event ({})\n", rc);
                break;
            }
        }

        return input;
    }


    // if (rc == 0) {
    //     printf("Event: %s %s %d\n",
    //            libevdev_event_type_get_name(input.type),
    //            libevdev_event_code_get_name(input.type, input.code),
    //            input.value);
    // }
}
