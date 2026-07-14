// Created by moisrex on 12/16/25.
module;
#include <cassert>
#include <cstdint>
#include <libudev.h>
#include <string_view>
#include <utility>
module foresight.devices.udev;

namespace {
    // constructing string_view with nullptr pointer is UB.
    [[nodiscard]] std::string_view viewify(char const* str) noexcept {
        return str == nullptr ? "" : str;
    }
} // namespace

fs8::udev::udev() noexcept : handle{udev_new()} {}

fs8::udev::udev(fs8::udev&& other) noexcept : handle{std::exchange(other.handle, nullptr)} {}

fs8::udev& fs8::udev::operator=(udev&& other) noexcept {
    if (&other == this) [[unlikely]] {
        return *this;
    }
    if (handle) {
        udev_unref(handle);
    }
    handle = std::exchange(other.handle, nullptr);
    return *this;
}

fs8::udev::~udev() noexcept {
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

fs8::udev fs8::udev::instance() noexcept {
    static fs8::udev instance;
    return instance;
}

//////////////////////////////////////////////////////////////////////////////////////////

fs8::udev_device::udev_device(::udev* ctx, char const* subsystem, char const* sysname) noexcept
  : dev(::udev_device_new_from_subsystem_sysname(ctx, subsystem, sysname)) {}

fs8::udev_device::udev_device(udev_list_entry const& entry) noexcept : udev_device{udev::instance().native(), entry.name().data()} {}

fs8::udev_device::udev_device(::udev* ctx, char type, dev_t devnum) noexcept : dev(::udev_device_new_from_devnum(ctx, type, devnum)) {}

fs8::udev_device::udev_device(::udev* ctx) noexcept : dev(::udev_device_new_from_environment(ctx)) {}

fs8::udev_device::udev_device(::udev* udev, char const* const path) noexcept : dev{udev_device_new_from_syspath(udev, path)} {}

fs8::udev_device::udev_device(udev_device&& other) noexcept : dev{std::exchange(other.dev, nullptr)} {}

fs8::udev_device& fs8::udev_device::operator=(udev_device&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (dev != nullptr) {
        udev_device_unref(dev);
    }
    dev = std::exchange(other.dev, nullptr);
    return *this;
}

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
    // return udev_device{udev_device_get_udev(device), udev_device_get_syspath(device)};
    return udev_device{udev_device_ref(device)};
}

fs8::udev_device fs8::udev_device::parent(char const* const subsystem, char const* const devtype) const noexcept {
    auto const device = udev_device_get_parent_with_subsystem_devtype(dev, subsystem, devtype);
    if (device == nullptr) [[unlikely]] {
        return {}; // return empty device
    }

    // udev_device_get_parent() returns reference whose lifetime is tied to the child's lifetime,
    // we have to copy the device
    // return udev_device{udev_device_get_udev(device), udev_device_get_syspath(device)};
    return udev_device{udev_device_ref(device)};
}

std::string_view fs8::udev_device::subsystem() const noexcept {
    return viewify(udev_device_get_subsystem(dev));
}

std::string_view fs8::udev_device::devtype() const noexcept {
    return viewify(udev_device_get_devtype(dev));
}

std::string_view fs8::udev_device::syspath() const noexcept {
    return viewify(udev_device_get_syspath(dev));
}

std::string_view fs8::udev_device::sysname() const noexcept {
    return viewify(udev_device_get_sysname(dev));
}

std::string_view fs8::udev_device::sysnum() const noexcept {
    return viewify(udev_device_get_sysnum(dev));
}

std::string_view fs8::udev_device::devnode() const noexcept {
    return viewify(udev_device_get_devnode(dev));
}

std::string_view fs8::udev_device::property(char const* const name) const noexcept {
    return viewify(udev_device_get_property_value(dev, name));
}

std::string_view fs8::udev_device::driver() const noexcept {
    return viewify(udev_device_get_driver(dev));
}

std::string_view fs8::udev_device::action() const noexcept {
    return viewify(udev_device_get_action(dev));
}

std::string_view fs8::udev_device::sysattr(char const* const name) const noexcept {
    return viewify(udev_device_get_sysattr_value(dev, name));
}

bool fs8::udev_device::has_tag(char const* const name) const noexcept {
    return udev_device_has_tag(dev, name);
}

udev_device* fs8::udev_device::native() const noexcept {
    return dev;
}

fs8::udev_list_entry fs8::udev_device::properties() const noexcept {
    return udev_list_entry{::udev_device_get_properties_list_entry(dev)};
}

fs8::udev_list_entry fs8::udev_device::tags() const noexcept {
    return udev_list_entry{::udev_device_get_tags_list_entry(dev)};
}

fs8::udev_list_entry fs8::udev_device::sysattrs() const noexcept {
    return udev_list_entry{::udev_device_get_sysattr_list_entry(dev)};
}

fs8::udev_list_entry fs8::udev_device::devlinks() const noexcept {
    return udev_list_entry{::udev_device_get_devlinks_list_entry(dev)};
}

bool fs8::udev_device::is_initialized() const noexcept {
    return ::udev_device_get_is_initialized(dev) > 0;
}

std::uint64_t fs8::udev_device::usec_since_initialized() const noexcept {
    return ::udev_device_get_usec_since_initialized(dev);
}

std::uint64_t fs8::udev_device::seqnum() const noexcept {
    return ::udev_device_get_seqnum(dev);
}

//////////////////////////////////////////////////////////////////////////


std::string_view fs8::udev_list_entry::name() const noexcept {
    return viewify(::udev_list_entry_get_name(entry));
}

std::string_view fs8::udev_list_entry::value() const noexcept {
    return viewify(::udev_list_entry_get_value(entry));
}

//////////////////////////////////////////////////////////////////////////

fs8::udev_enumerate::udev_enumerate(udev const& dev) noexcept : handle{::udev_enumerate_new(dev.native())} {
    if (handle == nullptr) [[unlikely]] {
        code = -1;
    }
}

fs8::udev_enumerate::udev_enumerate() noexcept : udev_enumerate{udev::instance()} {}

fs8::udev_enumerate::udev_enumerate(fs8::udev_enumerate&& other) noexcept
  : handle{std::exchange(other.handle, nullptr)},
    code{std::exchange(other.code, 0)} {}

fs8::udev_enumerate& fs8::udev_enumerate::operator=(udev_enumerate&& other) noexcept {
    if (&other == this) [[unlikely]] {
        return *this;
    }
    if (handle) {
        udev_enumerate_unref(handle);
    }
    handle = std::exchange(other.handle, nullptr);
    code   = std::exchange(other.code, 0);
    return *this;
}

fs8::udev_enumerate::~udev_enumerate() noexcept {
    // don't check for is_valid instead of handle's nullness
    if (handle == nullptr) [[unlikely]] {
        return;
    }
    udev_enumerate_unref(handle);
}

udev_enumerate* fs8::udev_enumerate::native() const noexcept {
    return handle;
}

bool fs8::udev_enumerate::is_valid() const noexcept {
    return handle != nullptr && code >= 0;
}

fs8::udev_enumerate::operator bool() const noexcept {
    return is_valid();
}

fs8::udev_enumerate& fs8::udev_enumerate::match_subsystem(char const* const subsystem) noexcept {
    assert(is_valid());
    code = ::udev_enumerate_add_match_subsystem(handle, subsystem);
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::nomatch_subsystem(char const* subsystem) noexcept {
    assert(is_valid());
    code = ::udev_enumerate_add_nomatch_subsystem(handle, subsystem);
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::match_sysattr(char const* const name, char const* const value) noexcept {
    assert(is_valid());
    code = ::udev_enumerate_add_match_sysattr(handle, name, value);
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::nomatch_sysattr(char const* const name, char const* const value) noexcept {
    assert(is_valid());
    code = ::udev_enumerate_add_nomatch_sysattr(handle, name, value);
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::match_property(char const* const name, char const* const value) noexcept {
    assert(is_valid());
    code = ::udev_enumerate_add_match_property(handle, name, value);
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::match_sysname(char const* const sysname) noexcept {
    assert(is_valid());
    code = ::udev_enumerate_add_match_sysname(handle, sysname);
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::match_tag(char const* const tag) noexcept {
    assert(is_valid());
    code = ::udev_enumerate_add_match_tag(handle, tag);
    return *this;
}

fs8::udev_enumerate& fs8::udev_enumerate::match_parent(udev_device const& dev) noexcept {
    assert(is_valid());
    code = ::udev_enumerate_add_match_parent(handle, dev.native());
    return *this;
}

fs8::udev_list_entry fs8::udev_enumerate::list_entries() const noexcept {
    return udev_list_entry{::udev_enumerate_get_list_entry(handle)};
}

void fs8::udev_enumerate::scan_devices() noexcept {
    ::udev_enumerate_scan_devices(handle);
}

void fs8::udev_enumerate::scan_subsystems() noexcept {
    ::udev_enumerate_scan_subsystems(handle);
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

fs8::udev_monitor::udev_monitor(fs8::udev_monitor&& other) noexcept
  : mon{std::exchange(other.mon, nullptr)},
    fd{std::exchange(other.fd, 0)},
    code{std::exchange(other.code, 0)} {}

fs8::udev_monitor& fs8::udev_monitor::operator=(udev_monitor&& other) noexcept {
    if (&other == this) [[unlikely]] {
        return *this;
    }
    if (mon != nullptr) {
        udev_monitor_unref(mon);
    }
    mon  = std::exchange(other.mon, nullptr);
    fd   = std::exchange(other.fd, 0);
    code = std::exchange(other.code, 0);
    return *this;
}

fs8::udev_monitor::~udev_monitor() noexcept {
    // don't check for is_valid instead of mon's nullness.
    if (mon == nullptr) [[unlikely]] {
        return;
    }
    udev_monitor_unref(mon);
}

void fs8::udev_monitor::match_device(char const* const subsystem, char const* const type) noexcept {
    assert(is_valid());
    code = ::udev_monitor_filter_add_match_subsystem_devtype(mon, subsystem, type);
}

void fs8::udev_monitor::match_tag(char const* const name) noexcept {
    assert(is_valid());
    code = ::udev_monitor_filter_add_match_tag(mon, name);
}

bool fs8::udev_monitor::is_valid() const noexcept {
    return mon != nullptr && code >= 0;
}

int fs8::udev_monitor::file_descriptor() const noexcept {
    return fd;
}

void fs8::udev_monitor::enable() noexcept {
    assert(is_valid());
    code = ::udev_monitor_enable_receiving(mon);
}

fs8::udev_device fs8::udev_monitor::next_device() const noexcept {
    return fs8::udev_device{::udev_monitor_receive_device(mon)};
}

void fs8::udev_monitor::set_receive_buffer_size(int size) noexcept {
    assert(is_valid());
    ::udev_monitor_set_receive_buffer_size(mon, size);
}

void fs8::udev_monitor::filter_update() noexcept {
    assert(is_valid());
    ::udev_monitor_filter_update(mon);
}

void fs8::udev_monitor::filter_remove() noexcept {
    assert(is_valid());
    ::udev_monitor_filter_remove(mon);
}

/////////////////////////////////////////////////////////////////////////////////


fs8::udev_hwdb::udev_hwdb(::udev* ctx) : handle(::udev_hwdb_new(ctx)) {}

fs8::udev_hwdb::~udev_hwdb() {
    if (handle != nullptr) {
        ::udev_hwdb_unref(handle);
    }
}

fs8::udev_hwdb::udev_hwdb(udev_hwdb&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}

fs8::udev_list_entry fs8::udev_hwdb::get_properties(char const* modalias, unsigned int const flags) const noexcept {
    return udev_list_entry{::udev_hwdb_get_properties_list_entry(handle, modalias, flags)};
}

/////////////////////////////////////////////////////////////////////////////////


fs8::udev_queue::udev_queue(::udev* ctx) : handle(::udev_queue_new(ctx)) {}

fs8::udev_queue::~udev_queue() {
    if (handle != nullptr) {
        ::udev_queue_unref(handle);
    }
}

fs8::udev_queue::udev_queue(udev_queue&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}

bool fs8::udev_queue::is_active() const noexcept {
    return ::udev_queue_get_udev_is_active(handle) > 0;
}

bool fs8::udev_queue::is_empty() const noexcept {
    return ::udev_queue_get_queue_is_empty(handle) > 0;
}
