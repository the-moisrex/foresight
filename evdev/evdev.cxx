// Created by moisrex on 6/18/24.

module;
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <libevdev/libevdev.h>
#include <optional>
#include <unistd.h>
#include <utility>
module foresight.evdev;
import foresight.mods.caps;

using foresight::evdev;

std::string_view foresight::to_string(evdev_status const status) noexcept {
    using enum evdev_status;
    switch (status) {
        case unknown: return {"Unknown state."};
        case success: return {"Success."};
        case grab_failure: return {"Grabbing the input failed."};
        case invalid_file_descriptor: return {"The file descriptor specified is not valid."};
        case invalid_device: return {"The device is not valid."};
        case failed_setting_file_descriptor: return {"Failed to set the file descriptor."};
        case failed_to_open_file: return {"Failed to open the file."};
        default: return {"Invalid state."};
    }
}

evdev::evdev(std::filesystem::path const& file) noexcept {
    set_file(file);
}

evdev::evdev(evdev&& inp) noexcept
  : dev{std::exchange(inp.dev, nullptr)},
    status{std::exchange(inp.status, evdev_status::unknown)} {}

evdev& evdev::operator=(evdev&& other) noexcept {
    if (&other != this) {
        dev    = std::exchange(other.dev, nullptr);
        status = std::exchange(other.status, evdev_status::unknown);
    }
    return *this;
}

evdev::~evdev() noexcept {
    this->close();
}

void evdev::close() noexcept {
    if (dev != nullptr) {
        libevdev_grab(dev, LIBEVDEV_UNGRAB);
    }
    auto const file_descriptor = native_handle();
    if (dev != nullptr) {
        libevdev_free(dev);
        dev = nullptr;
    }
    if (file_descriptor >= 0) {
        ::close(file_descriptor);
    }
    status = evdev_status::unknown;
}

void evdev::set_file(std::filesystem::path const& file) noexcept {
    auto const new_fd = open(file.c_str(), O_RDWR);
    if (new_fd < 0) [[unlikely]] {
        this->close();
        status = evdev_status::failed_to_open_file;
        return;
        // throw std::invalid_argument(std::format("Failed to open file '{}'", file.string()));
    }
    set_file(new_fd);
}

void evdev::set_file(int const file) noexcept {
    this->close();
    if (file < 0) [[unlikely]] {
        status = evdev_status::invalid_file_descriptor;
        return;
    }
    if (dev == nullptr) {
        dev = libevdev_new();
        if (dev == nullptr) [[unlikely]] {
            this->close();
            status = evdev_status::invalid_device;
            return;
        }
    }

    if (int const res_rc = libevdev_set_fd(dev, file); res_rc < 0) [[unlikely]] {
        ::close(file);
        this->close();
        status = evdev_status::failed_setting_file_descriptor;
        return;
        // throw std::domain_error(
        //   std::format("Failed to set file for libevdev ({})\n", std::strerror(-res_rc)));
    }
    status = evdev_status::success;
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

void evdev::grab_input(bool const grab) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    if (libevdev_grab(dev, grab ? LIBEVDEV_GRAB : LIBEVDEV_UNGRAB) < 0) {
        status = evdev_status::grab_failure;
    }
}

std::string_view evdev::device_name() const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return invalid_device_name;
    }
    return libevdev_get_name(dev);
}

void evdev::device_name(std::string_view const new_name) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    libevdev_set_name(dev, new_name.data());
}

std::string_view evdev::physical_location() const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return invalid_device_location;
    }
    return libevdev_get_phys(dev);
}

void evdev::physical_location(std::string_view const new_name) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    libevdev_set_phys(dev, new_name.data());
}

void evdev::enable_event_type(ev_type const type) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    libevdev_enable_event_type(dev, type);
}

void evdev::enable_event_code(ev_type const type, code_type const code) noexcept {
    enable_event_code(type, code, nullptr);
}

void evdev::enable_event_code(ev_type const type, code_type const code, void const* const value) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    libevdev_enable_event_code(dev, type, code, value);
}

void evdev::enable_caps(dev_caps_view const inp_caps) noexcept {
    for (auto const& [type, codes, _] : inp_caps) {
        for (auto const code : codes) {
            enable_event_code(type, code);
        }
    }
}

void evdev::disable_event_type(ev_type const type) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    libevdev_disable_event_type(dev, type);
}

void evdev::disable_event_code(ev_type const type, code_type const code) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    libevdev_disable_event_code(dev, type, code);
}

void evdev::disable_caps(dev_caps_view const inp_caps) noexcept {
    for (auto const& [type, codes, _] : inp_caps) {
        for (auto const code : codes) {
            disable_event_code(type, code);
        }
    }
}

void evdev::apply_caps(dev_caps_view const inp_caps) noexcept {
    for (auto const& [type, codes, addition] : inp_caps) {
        if (addition) {
            for (auto const code : codes) {
                enable_event_code(type, code);
            }
        } else {
            for (auto const code : codes) {
                disable_event_code(type, code);
            }
        }
    }
}

bool evdev::has_event_type(ev_type const type) const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return false;
    }
    return libevdev_has_event_type(dev, type);
}

bool evdev::has_event_code(ev_type const type, code_type const code) const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return false;
    }
    return libevdev_has_event_code(dev, type, code);
}

bool evdev::has_cap(dev_cap_view const& inp_cap) const noexcept {
    for (code_type const code : inp_cap.codes) {
        if (!has_event_code(inp_cap.type, code)) {
            return false;
        }
    }
    return true;
}

// returns percentage
std::uint8_t evdev::match_cap(dev_cap_view const& inp_cap) const noexcept {
    double count = 0;
    for (code_type const code : inp_cap.codes) {
        if (has_event_code(inp_cap.type, code)) {
            ++count;
        }
    }
    return static_cast<std::uint8_t>(count / static_cast<double>(inp_cap.codes.size()) * 100);
}

bool evdev::has_caps(dev_caps_view const inp_caps) const noexcept {
    for (auto const& cap_view : inp_caps) {
        if (!has_cap(cap_view)) {
            return false;
        }
    }
    return true;
}

std::uint8_t evdev::match_caps(dev_caps_view const inp_caps) const noexcept {
    double count = 0;
    double all   = 0;
    for (auto const& [type, codes, _] : inp_caps) {
        for (code_type const code : codes) {
            if (has_event_code(type, code)) {
                ++count;
            }
        }
        all += static_cast<double>(codes.size());
    }
    return static_cast<std::uint8_t>(count / all * 100); // NOLINT(*-magic-numbers)
}

input_absinfo const* evdev::abs_info(code_type const code) const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return nullptr;
    }
    return libevdev_get_abs_info(dev, code);
}

std::optional<input_event> evdev::next() noexcept {
    input_event input;

    if (dev == nullptr) [[unlikely]] {
        return std::nullopt;
    }

    switch (libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &input)) {
        [[likely]] case LIBEVDEV_READ_STATUS_SUCCESS: { return input; }
        case LIBEVDEV_READ_STATUS_SYNC:
        case -EAGAIN: break;
        default: return std::nullopt;
    }

    return std::nullopt;
}

foresight::evdev_rank foresight::device(dev_caps_view const inp_caps) {
    auto       devs = foresight::devices(inp_caps);
    evdev_rank res;

    for (evdev_rank&& rank : devs) {
        if (rank.match > res.match) {
            res = std::move(rank);
        }
    }
    return res;
}

namespace {
    void trim(std::string_view& str) noexcept {
        auto const start = str.find_first_not_of(' ');
        auto const end   = str.find_last_not_of(' ');
        str              = str.substr(start, end - start + 1);
    }
} // namespace

foresight::evdev_rank foresight::device(std::string_view query) {
    trim(query);
    bool const is_path = query.starts_with('/');

    if (is_path) {
        std::filesystem::path const path{query};
        if (exists(path)) {
            return {.match = 100, .dev = evdev{path}};
        }
    }

    return device(caps_of(query));
}
