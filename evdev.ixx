// Created by moisrex on 6/18/24.

module;

#include <filesystem>
#include <libevdev/libevdev.h>
#include <string_view>

export module foresight.evdev;

/**
 * This is a wrapper for libevdev's related features
 */
export struct evdev {
    explicit evdev(std::filesystem::path const& file);
             evdev(evdev&&)          = default;
             evdev(evdev const&)     = delete;
    evdev&   operator=(evdev const&) = delete;
    evdev&   operator=(evdev&&)      = default;
    ~        evdev();

    /// change the input event file (for example /dev/input/eventX)
    void set_file(std::filesystem::path const& file);

    /// check if everything is okay
    [[nodiscard]] bool ok() const noexcept {
        return dev == nullptr;
    }

    void grab_input();

    /**
     * Retrieve the device's name, either as set by the caller or as read from
     * the kernel. The string returned is valid until libevdev_free() or until
     * libevdev_set_name(), whichever comes earlier.
     */
    [[nodiscard]] std::string_view device_name() const noexcept;

    [[nodiscard]] input_event next();

  private:
    int file_descriptor = -1;
    libevdev* dev = nullptr;
    bool grabbed = false;
};
