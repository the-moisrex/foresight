// Created by moisrex on 6/18/24.

module;
#include <algorithm>
#include <bits/this_thread_sleep.h>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <linux/limits.h>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>
module fs8.devices.evdev;
import fs8.devices.capabilities;
import fs8.log;

using fs8::evdev;

std::string_view fs8::to_string(evdev_status const status) noexcept {
    using enum evdev_status;
    switch (status) {
        case unknown: return {"Unknown state."};
        case success: return {"Success."};
        case success_grabbed: return {"Success, and also grabbed."};
        case grab_failure: return {"Grabbing the input failed."};
        case invalid_file_descriptor: return {"The file descriptor specified is not valid."};
        case invalid_device: return {"The device is not valid."};
        case failed_setting_file_descriptor: return {"Failed to set the file descriptor."};
        case failed_to_open_file: return {"Failed to open the file."};
        case not_matched: return {"Query doesn't match the device."};
        default: return {"Invalid state."};
    }
}

evdev::evdev(std::filesystem::path const& file) noexcept {
    set_file(file);
}

evdev::evdev(evdev&& inp) noexcept : dev{std::exchange(inp.dev, nullptr)}, status{std::exchange(inp.status, evdev_status::unknown)} {}

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
    if (is_fd_initialized()) {
        // just to be safe
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
    // O_NONBLOCK is useful and recommended when opening /dev/input/event* (so read() and epoll_wait() don’t
    // block forever).
    auto const new_fd = ::open(file.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (new_fd < 0) [[unlikely]] {
        this->close();
        status = evdev_status::failed_to_open_file;
        return;
    }
    set_file(new_fd);
}

void evdev::set_file(int const file) noexcept {
    using enum evdev_status;
    this->close();
    if (file < 0) [[unlikely]] {
        status = invalid_file_descriptor;
        return;
    }
    int const res_rc = libevdev_new_from_fd(file, &dev);
    if (dev == nullptr) [[unlikely]] {
        this->close();
        status = invalid_device;
        return;
    }
    if (res_rc < 0) [[unlikely]] {
        // res_rc is now -errno
        ::close(file);
        this->close();
        status = failed_setting_file_descriptor;
        return;
    }
    status = success;
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

bool evdev::is_fd_initialized() const noexcept {
    return native_handle() != -1;
}

void evdev::grab_input(bool const grab) noexcept {
    using enum evdev_status;
    if (!is_ok()) [[unlikely]] {
        return;
    }
    if (!grab && status == success) {
        return;
    }
    if (grab && status == success_grabbed) [[unlikely]] {
        // don't need to double grab
        return;
    }
    if (libevdev_grab(dev, grab ? LIBEVDEV_GRAB : LIBEVDEV_UNGRAB) < 0) [[unlikely]] {
        status = grab_failure;
        return;
    }
    status = grab ? success_grabbed : success;
}

fs8::grab_state evdev::grab() const noexcept {
    using enum grab_state;
    if (!is_ok()) {
        return error;
    }
    if (get_status() == evdev_status::success_grabbed) {
        return grabbing;
    }
    return not_grabbing;
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
    auto const* res = libevdev_get_phys(dev);
    if (res == nullptr) [[unlikely]] {
        return invalid_device_location;
    }
    return {res};
}

std::string_view evdev::unique_identifier() const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return invalid_unique_identifier;
    }
    auto const* res = libevdev_get_uniq(dev);
    if (res == nullptr) [[unlikely]] {
        return invalid_unique_identifier;
    }
    return {res};
}

void evdev::physical_location(std::string_view const new_name) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    libevdev_set_phys(dev, new_name.data());
}

void evdev::unique_identifier(std::string_view const new_name) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    libevdev_set_uniq(dev, new_name.data());
}

void evdev::enable_event_type(ev_type const type) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    if (libevdev_enable_event_type(dev, type) != 0) [[unlikely]] {
        status = evdev_status::failed_to_set_options;
    }
}

void evdev::enable_event_code(ev_type const type, code_type const code) noexcept {
    enable_event_code(type, code, nullptr);
}

void evdev::enable_event_code(ev_type const type, code_type const code, void const* const value) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    if (libevdev_enable_event_code(dev, type, code, value) != 0) [[unlikely]] {
        status = evdev_status::failed_to_set_options;
    }
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
    if (libevdev_disable_event_type(dev, type) != 0) [[unlikely]] {
        status = evdev_status::failed_to_set_options;
    }
}

void evdev::disable_event_code(ev_type const type, code_type const code) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    if (libevdev_disable_event_code(dev, type, code) != 0) [[unlikely]] {
        status = evdev_status::failed_to_set_options;
    }
}

void evdev::disable_caps(dev_caps_view const inp_caps) noexcept {
    for (auto const& [type, codes, _] : inp_caps) {
        for (auto const code : codes) {
            disable_event_code(type, code);
        }
    }
}

void evdev::apply_caps(dev_caps_view const inp_caps) noexcept {
    using enum caps_action;
    for (auto const& [type, codes, action] : inp_caps) {
        switch (action) {
            case append:
                for (auto const code : codes) {
                    enable_event_code(type, code);
                }
                break;
            case remove_codes:
                for (auto const code : codes) {
                    disable_event_code(type, code);
                }
                break;
            case remove_type: disable_event_type(type); break;
        }
    }
}

bool evdev::has_event_type(ev_type const type) const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return false;
    }
    return libevdev_has_event_type(dev, type) == 1;
}

bool evdev::has_event_code(ev_type const type, code_type const code) const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return false;
    }
    return libevdev_has_event_code(dev, type, code) == 1;
}

bool evdev::has_cap(dev_cap_view const& inp_cap) const noexcept {
    return std::ranges::all_of(inp_cap.codes, [this, type = inp_cap.type](auto const code) {
        return has_event_code(type, code);
    });
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
    return std::ranges::all_of(inp_caps, [this](auto const& inp_cap) noexcept {
        return has_cap(inp_cap);
    });
}

std::uint8_t evdev::match_caps(dev_caps_view const inp_caps) const noexcept {
    using enum caps_action;
    double count = 0;
    double all   = 0;
    for (auto const& [type, codes, action] : inp_caps) {
        switch (action) {
            case append:
                all += static_cast<double>(codes.size());
                for (code_type const code : codes) {
                    if (has_event_code(type, code)) {
                        ++count;
                    }
                }
                break;
            case remove_codes:
                for (code_type const code : codes) {
                    if (has_event_code(type, code)) {
                        --count;
                    }
                }
                break;
            case remove_type:
                if (has_event_type(type)) {
                    --count;
                }
                break;
        }
    }
    if (all == 0) {
        return 100;
    }
    return static_cast<std::uint8_t>(std::max(0.0, count) / all * 100.0); // NOLINT(*-magic-numbers)
}

input_absinfo const* evdev::abs_info(code_type const code) const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return nullptr;
    }
    return libevdev_get_abs_info(dev, code);
}

bool evdev::has_abs_info(code_type const code) const noexcept {
    return this->abs_info(code) != nullptr;
}

void evdev::abs_info(code_type const abs_code, input_absinfo const& abs_info) noexcept {
    if (is_fd_initialized()) {
        return;
    }
    // Decide if this should implicitly enable the code if not present,
    // or require it to be enabled first.
    if (!has_event_code(EV_ABS, abs_code)) {
        enable_event_code(EV_ABS, abs_code, &abs_info);
    }

    // Code exists, update its info
    libevdev_set_abs_info(dev, abs_code, &abs_info);
}

bool evdev::operator==(evdev const& other) const noexcept {
    auto const dev2 = other.dev;
    if (dev == dev2) {
        return true; // Same pointer (trivial case, zero cost)
    }
    if (!dev || !dev2) [[unlikely]] {
        return false;
    }

    // Primary: phys path (most reliable and usually present)
    char const* phys1 = libevdev_get_phys(dev);
    char const* phys2 = libevdev_get_phys(dev2);

    if (phys1 && phys2) {
        return std::strcmp(phys1, phys2) == 0;
    }
    if (phys1 || phys2) [[unlikely]] {
        return false; // One has phys, the other doesn't → different
    }

    // Fallback: uniq (serial-like identifier)
    char const* uniq1 = libevdev_get_uniq(dev);
    char const* uniq2 = libevdev_get_uniq(dev2);

    if (uniq1 && uniq2) {
        return std::strcmp(uniq1, uniq2) == 0;
    }
    if (uniq1 || uniq2) {
        return false;
    }

    // Last resort: device ID (same model, not guaranteed same instance)
    return (libevdev_get_id_bustype(dev) == libevdev_get_id_bustype(dev2))
           && (libevdev_get_id_vendor(dev) == libevdev_get_id_vendor(dev2))
           && (libevdev_get_id_product(dev) == libevdev_get_id_product(dev2))
           && (libevdev_get_id_version(dev) == libevdev_get_id_version(dev2));
}

std::optional<input_event> evdev::next() noexcept {
    input_event input{};

    if (dev == nullptr) [[unlikely]] {
        return std::nullopt;
    }

    switch (libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &input)) {
        [[likely]] case LIBEVDEV_READ_STATUS_SUCCESS: { return input; }
        [[unlikely]] case LIBEVDEV_READ_STATUS_SYNC: {
            // handling 'SYN_DROPPED's:
            int rc = LIBEVDEV_READ_STATUS_SYNC;
            while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &input);
            }
        }
        case -EAGAIN: break;
        default: return std::nullopt;
    }

    return std::nullopt;
}

bool evdev::send_event(input_event const& event) const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return false;
    }
    int const fd = libevdev_get_fd(dev);
    if (fd < 0) [[unlikely]] {
        return false;
    }
    return ::write(fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event));
}

bool evdev::send_event(event_type::type_type const  type,
                       event_type::code_type const  code,
                       event_type::value_type const value) const noexcept {
    input_event event{};
    event.type  = type;
    event.code  = code;
    event.value = value;
    return send_event(event);
}

/// Check if a freshly-opened device can be grabbed without disrupting a grab
/// this process already holds. Always leaves the device ungrabbed afterwards.
bool fs8::test_grab(evdev& dev) noexcept {
    dev.grab_input(true);
    if (dev.get_status() == evdev_status::grab_failure) {
        return false;
    }
    dev.grab_input(false);
    return dev.get_status() == evdev_status::success;
}

/// Check if a device is usable as a source for a virtual device without
/// disrupting a grab that this process already holds.
bool fs8::is_usable(evdev& dev) noexcept {
    if (dev.get_status() == evdev_status::success_grabbed) {
        return true; // already grabbed by us; just copy from it
    }
    return test_grab(dev);
}

/// sysname of an open device (e.g. "event10"), derived from its fd.
std::string fs8::device_sysname(evdev const& dev) noexcept try {
    int const fd = dev.native_handle();
    if (fd < 0) [[unlikely]] {
        return {};
    }
    char       buf[PATH_MAX]{};
    auto const n = ::readlink(("/proc/self/fd/" + std::to_string(fd)).c_str(), buf, sizeof(buf) - 1);
    if (n <= 0) [[unlikely]] {
        return {};
    }
    buf[n] = '\0';
    std::string_view path{buf, static_cast<size_t>(n)};
    if (auto const pos = path.find_last_of('/'); pos != std::string_view::npos) {
        path.remove_prefix(pos + 1);
    }
    constexpr std::string_view deleted{" (deleted)"};
    if (path.ends_with(deleted)) {
        path.remove_suffix(deleted.size());
    }
    return std::string{path};
} catch (...) {
    return {};
}

/// Make an independent deep copy of a device so we can reshape it
/// (name/uniq/bustype/caps) without mutating the caller's device, which
/// may still be in use (e.g. an input_manager device that shares this
/// libevdev handle).
evdev fs8::clone_device(evdev const& src) noexcept try {
    if (!src.is_ok()) [[unlikely]] {
        return {};
    }
    auto* const raw  = src.device_ptr();
    libevdev*   copy = libevdev_new();
    if (copy == nullptr) [[unlikely]] {
        return {};
    }
    if (auto const* name = libevdev_get_name(raw); name != nullptr) {
        libevdev_set_name(copy, name);
    }
    if (auto const* phys = libevdev_get_phys(raw); phys != nullptr) {
        libevdev_set_phys(copy, phys);
    }
    if (auto const* uniq = libevdev_get_uniq(raw); uniq != nullptr) {
        libevdev_set_uniq(copy, uniq);
    }
    libevdev_set_id_bustype(copy, libevdev_get_id_bustype(raw));
    libevdev_set_id_vendor(copy, libevdev_get_id_vendor(raw));
    libevdev_set_id_product(copy, libevdev_get_id_product(raw));
    libevdev_set_id_version(copy, libevdev_get_id_version(raw));

    for (unsigned type = 0; type <= EV_MAX; ++type) {
        if (!libevdev_has_event_type(raw, type)) {
            continue;
        }
        if (libevdev_enable_event_type(copy, type) < 0) [[unlikely]] {
            libevdev_free(copy);
            return {};
        }
        auto const code_max = static_cast<unsigned>([&] {
            switch (type) {
                case EV_KEY: return KEY_MAX;
                case EV_REL: return REL_MAX;
                case EV_ABS: return ABS_MAX;
                case EV_MSC: return MSC_MAX;
                case EV_SW: return SW_MAX;
                case EV_LED: return LED_MAX;
                case EV_SND: return SND_MAX;
                default: return FF_MAX;
            }
        }());
        for (unsigned code = 0; code <= code_max; ++code) {
            if (!libevdev_has_event_code(raw, type, code)) {
                continue;
            }
            if (type == EV_ABS) {
                // An absolute axis can only be re-enabled with its
                // absinfo; skip axes that carry none.
                if (auto const* abs = libevdev_get_abs_info(raw, code)) {
                    if (libevdev_enable_event_code(copy, type, code, abs) < 0) [[unlikely]] {
                        libevdev_free(copy);
                        return {};
                    }
                }
                continue;
            }
            if (type == EV_REP) {
                // uinput needs no EV_REP (the kernel derives it from
                // the device); it also can't be enabled without data.
                continue;
            }
            if (libevdev_enable_event_code(copy, type, code, nullptr) < 0) [[unlikely]] {
                libevdev_free(copy);
                return {};
            }
        }
    }

    for (unsigned prop = 0; prop <= INPUT_PROP_MAX; ++prop) {
        if (libevdev_has_property(raw, prop) && libevdev_enable_property(copy, prop) < 0) [[unlikely]] {
            libevdev_free(copy);
            return {};
        }
    }

    return evdev{copy, evdev_status::success};
} catch (...) {
    return {};
}
