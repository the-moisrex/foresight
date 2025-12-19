// Created by moisrex on 12/16/25.
module;
#include <libudev.h>
#include <string_view>
module foresight.devices.udev;

fs8::udev::udev() noexcept : handle{udev_new()} {}

fs8::udev::~udev() {
    udev_unref(handle);
}

bool fs8::udev::is_valid() const noexcept {
    return handle != nullptr;
}

fs8::udev::operator bool() const noexcept {
    return is_valid();
}

udev* fs8::udev::native() const noexcept {
    return handle;
}

fs8::udev::udev(fs8::udev const& other) noexcept : handle{udev_ref(other.handle)} {}

fs8::udev& fs8::udev::operator=(udev const& other) noexcept {
    if (&other == this) [[unlikely]] {
        return *this;
    }
    udev_unref(handle);
    handle = udev_ref(other.handle);
    return *this;
}

fs8::udev fs8::udev::instance() {
    static fs8::udev instance;
    return instance;
}

//////////////////////////////////////////////////////////////////////////////////////////

fs8::udev_device::udev_device(::udev* udev, std::string_view const path) noexcept : dev{udev_device_new_from_syspath(udev, path.data())} {}

fs8::udev_device::~udev_device() noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    udev_device_unref(dev);
}

bool fs8::udev_device::is_valid() const noexcept {
    return dev != nullptr;
}

fs8::udev_device::operator bool() const noexcept {
    return is_valid();
}

fs8::udev_device fs8::udev_device::parent() const noexcept {
    auto const device = udev_device_get_parent(dev);
    if (device == nullptr) [[unlikely]] {
        return {}; // return empty device
    }

    // udev_device_get_parent() returns reference whose lifetime is tied to the child's lifetime,
    // we have to copy the device
    return udev_device{udev_device_get_udev(device), udev_device_get_syspath(device)};
}

fs8::udev_device fs8::udev_device::parent(std::string_view const subsystem, std::string_view const devtype) const noexcept {
    auto const device = udev_device_get_parent_with_subsystem_devtype(dev, subsystem.data(), devtype.size() ? devtype.data() : nullptr);
    if (device == nullptr) [[unlikely]] {
        return {}; // return empty device
    }

    // udev_device_get_parent() returns reference whose lifetime is tied to the child's lifetime,
    // we have to copy the device
    return udev_device{udev_device_get_udev(device), udev_device_get_syspath(device)};
}

std::string_view fs8::udev_device::subsystem() const noexcept {
    return udev_device_get_subsystem(dev);
}

std::string_view fs8::udev_device::devtype() const noexcept {
    return udev_device_get_devtype(dev);
}

std::string_view fs8::udev_device::syspath() const noexcept {
    return udev_device_get_syspath(dev);
}

std::string_view fs8::udev_device::sysname() const noexcept {
    return udev_device_get_sysname(dev);
}

std::string_view fs8::udev_device::sysnum() const noexcept {
    return udev_device_get_sysnum(dev);
}

std::string_view fs8::udev_device::devnode() const noexcept {
    return udev_device_get_devnode(dev);
}

std::string_view fs8::udev_device::property(std::string_view const name) const noexcept {
    return udev_device_get_property_value(dev, name.data());
}

std::string_view fs8::udev_device::driver() const noexcept {
    return udev_device_get_driver(dev);
}

std::string_view fs8::udev_device::action() const noexcept {
    return udev_device_get_action(dev);
}

std::string_view fs8::udev_device::sysattr(std::string_view const name) const noexcept {
    return udev_device_get_sysattr_value(dev, name.data());
}

bool fs8::udev_device::has_tag(std::string_view const name) const noexcept {
    return udev_device_has_tag(dev, name.data());
}

udev_device* fs8::udev_device::native() const noexcept {
    return dev;
}

//////////////////////////////////////////////////////////////////////////

fs8::udev_enumerate::udev_enumerate(udev const& dev) noexcept : handle{::udev_enumerate_new(dev.native())} {
    if (handle == nullptr) [[unlikely]] {
        code = -1;
    }
}

fs8::udev_enumerate::udev_enumerate() noexcept : udev_enumerate{udev::instance()} {}

fs8::udev_enumerate::~udev_enumerate() noexcept {
    if (!is_valid()) [[unlikely]] {
        return;
    }
    udev_enumerate_unref(handle);
}

udev_enumerate* fs8::udev_enumerate::native() const noexcept {
    return handle;
}

bool fs8::udev_enumerate::is_valid() const noexcept {
    return code >= 0;
}

fs8::udev_enumerate::operator bool() const noexcept {
    return is_valid();
}

fs8::udev_enumerate& fs8::udev_enumerate::match_subsystem(std::string_view const subsystem) noexcept {
    if (!is_valid()) [[unlikely]] {
        return *this;
    }
    code = ::udev_enumerate_add_match_subsystem(handle, subsystem.data());
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::nomatch_subsystem(std::string_view const subsystem) noexcept {
    if (!is_valid()) [[unlikely]] {
        return *this;
    }
    code = ::udev_enumerate_add_nomatch_subsystem(handle, subsystem.data());
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::match_sysattr(std::string_view const name, std::string_view const value) noexcept {
    if (!is_valid()) [[unlikely]] {
        return *this;
    }
    code = ::udev_enumerate_add_match_sysattr(handle, name.data(), value.empty() ? nullptr : value.data());
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::nomatch_sysattr(std::string_view const name, std::string_view const value) noexcept {
    if (!is_valid()) [[unlikely]] {
        return *this;
    }
    code = ::udev_enumerate_add_nomatch_sysattr(handle, name.data(), value.empty() ? nullptr : value.data());
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::match_property(std::string_view const name, std::string_view const value) noexcept {
    if (!is_valid()) [[unlikely]] {
        return *this;
    }
    code = ::udev_enumerate_add_match_property(handle, name.data(), value.empty() ? nullptr : value.data());
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::match_sysname(std::string_view const sysname) noexcept {
    if (!is_valid()) [[unlikely]] {
        return *this;
    }
    code = ::udev_enumerate_add_match_sysname(handle, sysname.data());
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::match_tag(std::string_view const tag) noexcept {
    if (!is_valid()) [[unlikely]] {
        return *this;
    }
    code = ::udev_enumerate_add_match_tag(handle, tag.data());
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::match_parent(udev_device const& dev) noexcept {
    if (!is_valid()) [[unlikely]] {
        return *this;
    }
    code = ::udev_enumerate_add_match_parent(handle, dev.native());
    return *this;
}

//////////////////////////////////////////////////////////////////////////////////////////////

// "udev" → monitors events from the udev daemon (processed device events; most common).
// "kernel" → monitors raw events directly from the kernel (unprocessed; rarely used).
fs8::udev_monitor::udev_monitor(udev const& dev) noexcept : mon{::udev_monitor_new_from_netlink(dev.native(), "udev")} {
    if (mon == nullptr) [[unlikely]] {
        code = -1;
        return;
    }
    fd = ::udev_monitor_get_fd(mon);
    if (fd < 0) {
        code = fd;
    }
}

fs8::udev_monitor::udev_monitor() noexcept : udev_monitor{udev::instance()} {}

fs8::udev_monitor::~udev_monitor() noexcept {
    if (!is_valid()) {
        return;
    }
    udev_monitor_unref(mon);
}

void fs8::udev_monitor::match_device(std::string_view subsystem, std::string_view type) noexcept {
    if (!is_valid()) [[unlikely]] {
        return;
    }
    auto const s = type.empty() ? nullptr : type.data();
    code         = ::udev_monitor_filter_add_match_subsystem_devtype(mon, subsystem.data(), s);
}

void fs8::udev_monitor::match_tag(std::string_view const name) noexcept {
    if (!is_valid()) [[unlikely]] {
        return;
    }
    code = ::udev_monitor_filter_add_match_tag(mon, name.data());
}

bool fs8::udev_monitor::is_valid() const noexcept {
    return code >= 0;
}

int fs8::udev_monitor::file_descriptor() const noexcept {
    return fd;
}

void fs8::udev_monitor::enable() noexcept {
    if (!is_valid()) [[unlikely]] {
        return;
    }
    code = ::udev_monitor_enable_receiving(mon);
}

fs8::udev_device fs8::udev_monitor::next_device() const noexcept {
    return fs8::udev_device{::udev_monitor_receive_device(mon)};
}
