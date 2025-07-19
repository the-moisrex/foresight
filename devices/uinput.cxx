// Created by moisrex on 6/29/24.

module;
#include <cassert>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <libevdev/libevdev-uinput.h>
#include <print>
#include <ranges>
#include <system_error>
module foresight.uinput;
import foresight.mods.event;
import foresight.main.log;

using foresight::basic_uinput;

basic_uinput::basic_uinput(evdev const& evdev_dev, std::filesystem::path const& file) noexcept
  : basic_uinput(evdev_dev.device_ptr(), file) {}

basic_uinput::basic_uinput(evdev const& evdev_dev, int const file_descriptor) noexcept
  : basic_uinput(evdev_dev.device_ptr(), file_descriptor) {}

basic_uinput::basic_uinput(libevdev const* evdev_dev, std::filesystem::path const& file) noexcept {
    auto const file_descriptor = open(file.c_str(), O_RDWR | O_NONBLOCK);
    if (file_descriptor < 0) [[unlikely]] {
        err_code = static_cast<std::errc>(errno);
        dev      = nullptr;
        return;
    }
    set_device(evdev_dev, file_descriptor);
}

basic_uinput::basic_uinput(libevdev const* evdev_dev, int const file_descriptor) noexcept {
    set_device(evdev_dev, file_descriptor);
}

void basic_uinput::close() noexcept {
    err_code = std::errc{};
    if (dev != nullptr) {
        libevdev_uinput_destroy(dev);
        dev = nullptr;
    }
}

std::error_code basic_uinput::error() const noexcept {
    return std::make_error_code(err_code);
}

bool basic_uinput::is_ok() const noexcept {
    return dev != nullptr && err_code == std::errc{};
}

void basic_uinput::set_device(libevdev const* evdev_dev, int const file_descriptor) noexcept {
    close();
    if (evdev_dev == nullptr) [[unlikely]] {
        err_code = std::errc::invalid_argument;
        return;
    }

    // If uinput_fd is @ref LIBEVDEV_UINPUT_OPEN_MANAGED, libevdev_uinput_create_from_device()
    // will open @c /dev/uinput in read/write mode and manage the file descriptor.
    // Otherwise, uinput_fd must be opened by the caller and opened with the
    // appropriate permissions.
    if (auto const ret = libevdev_uinput_create_from_device(evdev_dev, file_descriptor, &dev); ret != 0)
      [[unlikely]]
    {
        close();
        err_code = static_cast<std::errc>(-ret);
    }
    // log("Set Uinput: {} | {}", this->syspath(), this->devnode());
}

void basic_uinput::set_device(evdev const& inp_dev, int const file_descriptor) noexcept {
    set_device(inp_dev.device_ptr(), file_descriptor);
}

int basic_uinput::native_handle() const noexcept {
    if (!is_ok()) [[unlikely]] {
        return -1;
    }
    return libevdev_uinput_get_fd(dev);
}

std::string_view basic_uinput::syspath() const noexcept {
    if (!is_ok()) [[unlikely]] {
        return invalid_syspath;
    }
    return libevdev_uinput_get_syspath(dev);
}

std::string_view basic_uinput::devnode() const noexcept {
    if (!is_ok()) [[unlikely]] {
        return invalid_devnode;
    }
    return libevdev_uinput_get_devnode(dev);
}

bool basic_uinput::emit(ev_type const type, code_type const code, value_type const value) noexcept {
    assert(is_ok());
    if (auto const ret = libevdev_uinput_write_event(dev, type, code, value); ret != 0) [[unlikely]] {
        err_code = static_cast<std::errc>(-ret);
        return false;
    }
    return true;
}

bool basic_uinput::emit(input_event const& event) noexcept {
    assert(is_ok());
    return emit(event.type, event.code, event.value);
}

bool basic_uinput::emit(event_type const& event) noexcept {
    return emit(event.native());
}

bool basic_uinput::emit_syn() noexcept {
    return emit(EV_SYN, SYN_REPORT, 0);
}

void basic_uinput::init(dev_caps_view const caps_view) noexcept {
    evdev_rank best{};
    for (evdev_rank&& cur : devices(caps_view) | only_matching(50) | only_ok()) {
        if (cur.match >= best.match) {
            best = std::move(cur);
        }
    }
    if (best.match != 100) { // 100%
        best.dev.apply_caps(caps_view);
    }
    std::string new_name;
    new_name += best.dev.device_name();
    if (best.dev.ok()) {
        new_name += " (Copied)";
    } else {
        new_name = "Virtual Device";
        best.dev.init_new();
    }
    best.dev.device_name(new_name);
    log("Init uinput: {}", new_name);
    this->set_device(best.dev);
}

foresight::context_action basic_uinput::operator()(event_type const& event) noexcept {
    using enum context_action;
    // log("{}: {} {} {}", devnode(), event.type_name(), event.code_name(), event.value());
    if (!emit(event)) [[unlikely]] {
        return ignore_event;
    }
    return next;
}
