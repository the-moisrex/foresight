// Created by moisrex on 6/18/24.

module;
#include <atomic>
#include <filesystem>
#include <libevdev/libevdev.h>
#include <optional>
#include <string_view>
#include <utility>
export module foresight.evdev;

/**
 * This is a wrapper for libevdev's related features
 */
export struct evdev {
    explicit evdev(std::filesystem::path const& file);

    evdev(evdev&& inp) noexcept
      : file_descriptor{std::exchange(inp.file_descriptor, -1)},
        dev{std::exchange(inp.dev, nullptr)},
        is_stopped{inp.is_stopped.load()},
        grabbed{inp.grabbed} {}

    evdev(evdev const&)            = delete;
    evdev& operator=(evdev const&) = delete;

    evdev& operator=(evdev&& other) noexcept {
        if (this != &other) {
            file_descriptor = std::exchange(other.file_descriptor, -1);
            dev             = std::exchange(other.dev, nullptr);
            is_stopped      = other.is_stopped.load();
            grabbed         = other.grabbed;
        }
        return *this;
    }

    ~evdev();

    /// change the input event file (for example /dev/input/eventX)
    void set_file(std::filesystem::path const& file);
    void set_file(int file);

    [[nodiscard]] int       native_handle() const noexcept;
    [[nodiscard]] libevdev* device_ptr() noexcept;

    /// check if everything is okay
    [[nodiscard]] bool ok() const noexcept {
        return dev == nullptr;
    }

    void grab_input();

    void stop(bool should_stop = true);

    [[nodiscard]] bool is_done() const noexcept {
        return is_stopped.load();
    }

    /**
     * Retrieve the device's name, either as set by the caller or as read from
     * the kernel. The string returned is valid until libevdev_free() or until
     * libevdev_set_name(), whichever comes earlier.
     */
    [[nodiscard]] std::string_view device_name() const noexcept;

    /**
     * Get a new input_event form the input device
     * @param sync set true if you don't want it to wait
     */
    [[nodiscard]] std::optional<input_event> next(bool sync = false) noexcept;

  private:
    int              file_descriptor = -1;
    libevdev*        dev             = nullptr;
    std::atomic_bool is_stopped      = false;
    bool             grabbed         = false;
};
