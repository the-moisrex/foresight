// Created by moisrex on 6/29/24.

module;
#include <array>
#include <cassert>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <libevdev/libevdev-uinput.h>
#include <linux/uinput.h>
#include <print>
#include <ranges>
#include <span>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
module fs8.devices.uinput;
import fs8.event;
import fs8.log;
import fs8.context;
import fs8.devices.queries;
import fs8.devices.udev;
import fs8.mods.input_manager;

using fs8::basic_uinput;
using fs8::uinput_access_result;

static constexpr std::string_view uinput_path = "/dev/uinput";

#define SYS_INPUT_DIR "/sys/devices/virtual/input/"

#ifndef UINPUT_IOCTL_BASE
#    define UINPUT_IOCTL_BASE 'U'
#endif

#ifndef UI_SET_PROPBIT
#    define UI_SET_PROPBIT _IOW(UINPUT_IOCTL_BASE, 110, int)
#endif

struct my_libevdev_uinput {
    int    fd;            /**< file descriptor to uinput */
    int    fd_is_managed; /**< do we need to close it? */
    char*  name;          /**< device name */
    char*  syspath;       /**< /sys path */
    char*  devnode;       /**< device node */
    time_t ctime[2];      /**< before/after UI_DEV_CREATE */
};

uinput_access_result fs8::verify_access_to_uinput() noexcept {
    using enum uinput_access_result;

    // 1. Try to open directly first
    int const fd = ::open(uinput_path.data(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) [[unlikely]] {
        if (errno == EACCES || errno == EPERM) {
            return permission_denied;
        }
        if (errno == ENOENT || errno == ENODEV) {
            return device_not_found;
        }
        return open_failed;
    }

    // 2. If opened, verify it is a character device using the file descriptor
    struct stat device_stat{};
    if (::fstat(fd, &device_stat) != 0 || !S_ISCHR(device_stat.st_mode)) [[unlikely]] {
        ::close(fd);
        return not_a_character_device;
    }

    ::close(fd);
    return available;
}

[[nodiscard]] std::string_view fs8::to_string(uinput_access_result const result) noexcept {
    using enum uinput_access_result;
    switch (result) {
        case available: return {"uinput is available and accessible"};
        case device_not_found: return {"/dev/uinput was not found; the uinput kernel module may not be loaded"};
        case permission_denied: return {"permission denied while accessing /dev/uinput"};
        case not_a_character_device: return {"/dev/uinput exists but is not a character device"};
        case open_failed: return {"failed to open /dev/uinput"};
        default: return {"unknown uinput access status"};
    }
}

basic_uinput::basic_uinput(evdev const& evdev_dev, std::filesystem::path const& file) noexcept
  : basic_uinput(evdev_dev.device_ptr(), file) {}

basic_uinput::basic_uinput(evdev const& evdev_dev, int const file_descriptor) noexcept
  : basic_uinput(evdev_dev.device_ptr(), file_descriptor) {}

basic_uinput::basic_uinput(libevdev const* evdev_dev, std::filesystem::path const& file) noexcept {
    auto const file_descriptor = ::open(file.c_str(), O_RDWR | O_NONBLOCK);
    if (file_descriptor < 0) [[unlikely]] {
        err_code = errno;
        dev      = nullptr;
        return;
    }
    set_device(evdev_dev, file_descriptor);
}

basic_uinput::basic_uinput(libevdev const* evdev_dev, int const file_descriptor) noexcept {
    set_device(evdev_dev, file_descriptor);
}

void basic_uinput::close() noexcept {
    if (dev != nullptr) {
        libevdev_uinput_destroy(dev);
        dev = nullptr;
    }
    if (owned_fd >= 0) {
        ::close(owned_fd);
        owned_fd = -1;
    }
}

std::error_code basic_uinput::error() const noexcept {
    return std::error_code{err_code, std::system_category()};
}

bool basic_uinput::is_ok() const noexcept {
    return dev != nullptr && err_code >= 0;
}

void basic_uinput::set_device(libevdev const* evdev_dev, int const file_descriptor) noexcept {
    close();
    err_code = 0;
    if (evdev_dev == nullptr) [[unlikely]] {
        err_code = static_cast<int>(std::errc::invalid_argument);
        log("Input device in null.");
        return;
    }

    // If uinput_fd is @ref LIBEVDEV_UINPUT_OPEN_MANAGED, libevdev_uinput_create_from_device()
    // will open @c /dev/uinput in read/write mode and manage the file descriptor.
    // Otherwise, uinput_fd must be opened by the caller and opened with the
    // appropriate permissions.
    if (auto const ret = libevdev_uinput_create_from_device(evdev_dev, file_descriptor, &dev); ret < 0) [[unlikely]] {
        err_code = -ret;
        log("Failed to create virtual device (uinput) from device ({}): {}", ret, this->error().message());
        log("File descriptor: {} (-2 == LIBEVDEV_UINPUT_OPEN_MANAGED)", file_descriptor);
        if (static_cast<std::errc>(err_code) == std::errc::no_such_file_or_directory) {
            log("Maybe it'll be fixed by `modprobe uinput`.");
        }
        close();
        return;
    }
    log("Init Virtual Device: '{}' from '{}'", this->devnode(), libevdev_get_name(evdev_dev));
}

void basic_uinput::set_device(evdev const& inp_dev, int const file_descriptor) noexcept {
    set_device(inp_dev.device_ptr(), file_descriptor);
}

namespace {
    my_libevdev_uinput* alloc_uinput_device(char const* name) noexcept {
        my_libevdev_uinput* uinput_dev = nullptr;

        uinput_dev = reinterpret_cast<my_libevdev_uinput*>(::calloc(1, sizeof(my_libevdev_uinput)));
        if (uinput_dev != nullptr) {
            uinput_dev->name = strdup(name);
            uinput_dev->fd   = -1;
        }

        return uinput_dev;
    }

    int is_event_device(dirent const* dent) {
        return strncmp("event", dent->d_name, 5) == 0;
    }

    int is_input_device(dirent const* dent) {
        return strncmp("input", dent->d_name, 5) == 0;
    }

    char* fetch_device_node(char const* path) {
        char*    devnode = nullptr;
        dirent** namelist;

        int const ndev = scandir(path, &namelist, is_event_device, alphasort);
        if (ndev <= 0) {
            return nullptr;
        }

        /* ndev should only ever be 1 */

        for (int i = 0; i < ndev; i++) {
            if (!devnode && asprintf(&devnode, "/dev/input/%s", namelist[i]->d_name) == -1) {
                devnode = nullptr;
            }
            ::free(namelist[i]);
        }

        ::free(namelist);

        return devnode;
    }

    int fetch_syspath_and_devnode(my_libevdev_uinput* uinput_dev) {
        dirent** namelist;
        int      ndev, i;
        int      rc;
        char     buf[sizeof(SYS_INPUT_DIR) + 64] = SYS_INPUT_DIR;

        rc = ioctl(uinput_dev->fd, UI_GET_SYSNAME(sizeof(buf) - strlen(SYS_INPUT_DIR)), &buf[strlen(SYS_INPUT_DIR)]);
        if (rc != -1) {
            uinput_dev->syspath = strdup(buf);
            uinput_dev->devnode = fetch_device_node(buf);
            return 0;
        }

        ndev = scandir(SYS_INPUT_DIR, &namelist, is_input_device, alphasort);
        if (ndev <= 0) {
            return -1;
        }

        for (i = 0; i < ndev; i++) {
            int fd;

            struct stat st;

            rc = snprintf(buf, sizeof(buf), "%s%s/name", SYS_INPUT_DIR, namelist[i]->d_name);
            if (rc < 0 || (size_t) rc >= sizeof(buf)) {
                continue;
            }

            /* created within time frame */
            fd = open(buf, O_RDONLY);
            if (fd < 0) {
                continue;
            }

            /* created before UI_DEV_CREATE, or after it finished */
            if (fstat(fd, &st) == -1 || st.st_ctime < uinput_dev->ctime[0] || st.st_ctime > uinput_dev->ctime[1]) {
                close(fd);
                continue;
            }

            auto const len = read(fd, buf, sizeof(buf));
            close(fd);
            if (len <= 0) {
                continue;
            }

            buf[len - 1] = '\0'; /* file contains \n */
            if (strcmp(buf, uinput_dev->name) == 0) {
                if (uinput_dev->syspath) {
                    /* FIXME: could descend into bit comparison here */
                    fs8::log("multiple identical devices found. syspath is unreliable");
                    break;
                }
                rc = snprintf(buf, sizeof(buf), "%s%s", SYS_INPUT_DIR, namelist[i]->d_name);
                if (rc < 0 || static_cast<size_t>(rc) >= sizeof(buf)) {
                    fs8::log("Invalid syspath, syspath is unreliable");
                    break;
                }

                uinput_dev->syspath = strdup(buf);
                uinput_dev->devnode = fetch_device_node(buf);
            }
        }

        for (i = 0; i < ndev; i++) {
            free(namelist[i]);
        }
        free(namelist);

        return uinput_dev->devnode ? 0 : -1;
    }

    int uinput_SETUP(int const fd, my_libevdev_uinput const* new_device) {
        uinput_setup setup{};
        strncpy(setup.name, new_device->name, UINPUT_MAX_NAME_SIZE - 1);
        setup.id.vendor      = 0;
        setup.id.product     = 0;
        setup.id.bustype     = BUS_VIRTUAL; // mark the device as virtual in the kernel
        setup.id.version     = 0;
        setup.ff_effects_max = 0;

        if (ioctl(fd, UI_DEV_SETUP, &setup) == 0) {
            errno = 0;
        }
        return errno;
    }

    /// Name for a virtual device. Repeated " (Virtual)" suffixes from chained
    /// foresight devices are collapsed so the name stays clean and short.
    std::string virtual_device_name(std::string_view const source_name) {
        static constexpr std::string_view suffix = " (Virtual)";
        std::string_view                  base   = source_name;
        if (base.ends_with(suffix)) {
            base.remove_suffix(suffix.size());
        }
        while (!base.empty() && base.back() == ' ') {
            base.remove_suffix(1);
        }
        constexpr size_t name_cap = UINPUT_MAX_NAME_SIZE - 1; // include the NUL
        std::string      res;
        res.reserve(name_cap);
        auto const keep = std::min(base.size(), name_cap - suffix.size());
        res.append(base.data(), keep);
        res += suffix;
        return res;
    }

    /// phys for a virtual device carrying the origin chain, so that the output
    /// of one foresight app can be the input of another (and so on).
    /// Format: "foresight:<entry>,<entry>,..." where the last entry is the
    /// immediate source's sysname. uinput has no way to set the kernel `uniq`
    /// field, so the chain lives in `phys` (settable via UI_SET_PHYS).
    std::string virtual_device_phys(fs8::evdev const& src) {
        static constexpr std::string_view prefix  = "foresight:";
        static constexpr std::string_view sep     = ",";
        static constexpr size_t           max_len = 512;

        std::string chain;
        auto const  source_phys = src.physical_location();
        if (source_phys.starts_with(prefix)) {
            // already one of our virtual devices: extend the chain
            chain += source_phys.substr(prefix.size());
        }
        auto const sysname = fs8::device_sysname(src);
        if (!sysname.empty()) {
            if (!chain.empty()) {
                chain += sep;
            }
            chain += sysname;
        }
        if (chain.empty()) {
            return {};
        }
        if (chain.size() > max_len) {
            // drop oldest entries until it fits
            auto first_sep = chain.find(sep);
            while (chain.size() > max_len && first_sep != std::string::npos) {
                chain.erase(0, first_sep + sep.size());
                first_sep = chain.find(sep);
            }
            if (chain.size() > max_len) {
                chain.resize(max_len);
            }
        }
        return prefix + chain;
    }

    /// RAII ownership of a /dev/uinput fd that libevdev will not close (it is
    /// caller-provided). Released (adopted) once the device owns it, otherwise
    /// closed on scope exit — even when an exception is unwinding.
    struct fd_guard {
        int fd = -1;

        ~fd_guard() {
            if (fd >= 0) {
                ::close(fd);
            }
        }

        int release() noexcept {
            int const ret = fd;
            fd            = -1;
            return ret;
        }
    };

    /// Find a device matching the query via udev enumeration.
    fs8::evdev find_device_from_query(fs8::device_query const& inp_query) {
        auto edev = fs8::device(inp_query);
        if (!edev.is_ok()) [[unlikely]] {
            return {};
        }
        // `initialize` may leave the device grabbed when the query asks for
        // it; a freshly opened handle must be released before we copy it.
        if (!fs8::test_grab(edev)) {
            return {};
        }
        return edev;
    }

} // namespace

/// Copy a matching device into a virtual (uinput) device, applying caps.
/// If `best` is not valid, falls back to an empty device and applies caps.
/// The source device is deep-cloned; it is never modified or freed.
bool fs8::finalize_device(basic_uinput& self, evdev const& best, dev_caps_view const caps_view) noexcept try {
    using enum caps_action;
    if (best.is_ok()) {
        // Work on an independent copy: the caller (e.g. input_manager) may
        // still hold the source device, and we must not rename or re-cap it.
        auto clone = fs8::clone_device(best);
        if (!clone.is_ok()) [[unlikely]] {
            log("  Failed to clone device: {}", best.device_name());
            return false;
        }

        // A virtual device: standard kernel marker and a clean name.
        clone.device_name(virtual_device_name(best.device_name()));
        libevdev_set_id_bustype(clone.device_ptr(), BUS_VIRTUAL);

        // The origin chain lives in `phys` (uinput has no way to set the
        // kernel `uniq` field). Build the chain string before opening the
        // fd so nothing throws between the open and the ownership transfer.
        // UI_SET_PHYS must precede UI_DEV_CREATE, so we open /dev/uinput
        // ourselves, stamp the phys, and hand the fd to libevdev (which
        // keeps it open for the device's lifetime but never closes it —
        // that's our `owned_fd`).
        std::string const phys = virtual_device_phys(best);
        int               fd   = -1;
        if (!phys.empty()) [[likely]] {
            fd = ::open(uinput_path.data(), O_RDWR | O_CLOEXEC);
            if (fd < 0) [[unlikely]] {
                log("  Failed to open {} for the origin chain: {}", uinput_path, std::strerror(errno));
                return false;
            }
        }
        fd_guard guard{fd};
        if (!phys.empty() && ::ioctl(guard.fd, UI_SET_PHYS, phys.data()) == -1) [[unlikely]] {
            log("  Failed to set phys (origin chain) on the virtual device: {}", std::strerror(errno));
            return false;
        }

        for (auto const& [type, codes, action] : caps_view) {
            switch (action) {
                case append: {
                    if (codes.empty()) [[unlikely]] {
                        // Possible EV_MAX
                        break;
                    }
                    auto* dev_ptr = clone.device_ptr();
                    if (libevdev_has_event_type(dev_ptr, type) == 0) {
                        libevdev_enable_event_type(dev_ptr, type);
                    }
                    for (auto const code : codes) {
                        if (libevdev_has_event_code(dev_ptr, type, code) == 0) {
                            libevdev_enable_event_code(dev_ptr, type, code, nullptr);
                            log("  Enabled: {} {}", libevdev_event_type_get_name(type), libevdev_event_code_get_name(type, code));
                        }
                    }
                    break;
                }
                case remove_codes:
                case remove_type: break;
            }
        }
        self.set_device(clone, guard.fd < 0 ? LIBEVDEV_UINPUT_OPEN_MANAGED : guard.fd);
        if (!self.is_ok()) [[unlikely]] {
            log("  Device initialization failed: {}", clone.device_name());
            log("  Error: {}", self.error().message());
            return false;                // guard closes the fd
        }
        self.owned_fd = guard.release(); // close() releases it when the device is destroyed
    } else {
        self.set_device();
        self.apply_caps(caps_view);
        if (!self.is_ok()) [[unlikely]] {
            log("  Device init failed.");
            log("  Error: {}", self.error().message());
            return false;
        }
    }
    return true;
} catch (...) {
    log("  Exception while creating the virtual device.");
    return false;
}

void basic_uinput::set_device(int fd, std::string_view const name) noexcept {
    close();
    err_code = 0;

    bool const close_fd_on_error = fd == LIBEVDEV_UINPUT_OPEN_MANAGED;

    my_libevdev_uinput* mdev = alloc_uinput_device(name.data());
    if (mdev == nullptr) {
        err_code = ENOMEM;
        log("  allocation failure: {}", this->error().message());
        return;
    }

    if (fd == LIBEVDEV_UINPUT_OPEN_MANAGED) {
        fd = ::open("/dev/uinput", O_RDWR | O_CLOEXEC);
        if (fd < 0) [[unlikely]] {
            err_code = errno;
            log("  opening /dev/uinput failure: {}", this->error().message());
            return;
        }
        mdev->fd_is_managed = 1;
    } else if (fd < 0) [[unlikely]] {
        errno    = EBADF;
        err_code = errno;
        log("  Invalid fd {}; error: ", fd, this->error().message());
        return;
    }

    if (unsigned int uinput_version = 0; ::ioctl(fd, UI_GET_VERSION, &uinput_version) == 0 && uinput_version < 5U) [[unlikely]] {
        log("Kernel needs to supports uinput version 5, but it doesn't now.");
        return;
    }

    if (uinput_SETUP(fd, mdev) != 0) [[unlikely]] {
        err_code = errno;
        libevdev_uinput_destroy(reinterpret_cast<libevdev_uinput*>(mdev));
        if (fd != -1 && close_fd_on_error) {
            ::close(fd);
        }
        log("  uinput setup failure: {}", this->error().message());
        return;
    }

    /* ctime notes time before/after ioctl to help us filter out devices
       when traversing /sys/devices/virtual/input to find the device
       node.

       this is in seconds, so ctime[0]/[1] will almost always be
       identical but /sys doesn't give us sub-second ctime so...
     */
    mdev->ctime[0] = time(nullptr);

    if (::ioctl(fd, UI_DEV_CREATE, nullptr) == -1) [[unlikely]] {
        err_code = errno;
        libevdev_uinput_destroy(reinterpret_cast<libevdev_uinput*>(mdev));
        if (fd != -1 && close_fd_on_error) {
            ::close(fd);
        }
        log("  ioctl failure: {}", this->error().message());
        return;
    }

    mdev->ctime[1] = time(nullptr);
    mdev->fd       = fd;

    if (fetch_syspath_and_devnode(mdev) == -1) [[unlikely]] {
        errno    = ENODEV;
        err_code = errno;
        libevdev_uinput_destroy(reinterpret_cast<libevdev_uinput*>(mdev));
        if (fd != -1 && close_fd_on_error) {
            ::close(fd);
        }
        log("  Unable to fetch syspath or device node; error: {}", this->error().message());
        return;
    }

    this->dev = reinterpret_cast<libevdev_uinput*>(mdev);
    log("Init Empty Virtual Device: {}", this->devnode());
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

void basic_uinput::enable_event_type(ev_type const type) noexcept {
    if (::ioctl(native_handle(), UI_SET_EVBIT, type) == -1) [[unlikely]] {
        err_code = errno;
        log("  Failed to enable type {}: {}", libevdev_event_type_get_name(type), error().message());
    }
}

void basic_uinput::enable_event_code(ev_type const type, code_type const code) noexcept {
    /* uinput can't set EV_REP */
    if (type == EV_REP) [[unlikely]] {
        log("  uinput can't set EV_REP");
        return;
    }

    unsigned long uinput_bit{};

    switch (type) {
        case EV_KEY: uinput_bit = UI_SET_KEYBIT; break;
        case EV_REL: uinput_bit = UI_SET_RELBIT; break;
        case EV_ABS: uinput_bit = UI_SET_ABSBIT; break;
        case EV_MSC: uinput_bit = UI_SET_MSCBIT; break;
        case EV_LED: uinput_bit = UI_SET_LEDBIT; break;
        case EV_SND: uinput_bit = UI_SET_SNDBIT; break;
        case EV_FF: uinput_bit = UI_SET_FFBIT; break;
        case EV_SW:
            uinput_bit = UI_SET_SWBIT;
            break;
        [[unlikely]] default:
            errno    = EINVAL;
            err_code = errno;
            log("  Invalid type: {}", type);
            return;
    }

    if (::ioctl(native_handle(), uinput_bit, code) == -1) [[unlikely]] {
        err_code = errno;
        log("  Failed to enable: {} {}", libevdev_event_type_get_name(type), libevdev_event_code_get_name(type, code));
        return;
    }
    log("  Enabled: {} {}", libevdev_event_type_get_name(type), libevdev_event_code_get_name(type, code));
}

void basic_uinput::enable_caps(dev_caps_view const inp_caps) noexcept {
    for (auto const& [type, codes, _] : inp_caps) {
        for (auto const code : codes) {
            enable_event_code(type, code);
        }
    }
}

void basic_uinput::set_abs(code_type const code, input_absinfo const& abs_info) noexcept {
    uinput_abs_setup abs_setup{};
    abs_setup.code    = code;
    abs_setup.absinfo = abs_info;
    if (::ioctl(native_handle(), UI_ABS_SETUP, &abs_setup) != 0) {
        err_code = errno;
    }
}

void basic_uinput::apply_caps(dev_caps_view const inp_caps) noexcept {
    using enum caps_action;
    for (auto const& [type, codes, action] : inp_caps) {
        switch (action) {
            case append:
                if (!codes.empty()) {
                    enable_event_type(type);
                    for (auto const code : codes) {
                        enable_event_code(type, code);
                    }
                }
                break;
            case remove_codes:
            case remove_type: break;
        }
    }
}

bool basic_uinput::emit(ev_type const type, code_type const code, value_type const value) noexcept {
    assert(is_ok());
    if (auto const ret = libevdev_uinput_write_event(dev, type, code, value); ret < 0) [[unlikely]] {
        err_code = -ret;
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

bool basic_uinput::init(dev_caps_view const caps_view) noexcept {
    // don't re-initialize
    if (is_ok()) {
        return true;
    }
    return set_device_from(caps_view);
}

bool basic_uinput::set_device_from(dev_caps_view const caps_view) noexcept {
    // Constrain the search to the input subsystem (caps alone don't say where
    // to look), then pick the best matching device via the query system.
    std::array<fs8::query_term, 1> fields  = {fs8::subsystem("input")};
    fs8::device_query const        inp_query{.fields = std::span<fs8::query_term const>{fields}, .caps = caps_view};
    return set_device_from(inp_query);
}

bool basic_uinput::set_device_from(device_query const& inp_query) noexcept try {
    auto best = find_device_from_query(inp_query);
    if (!best.is_ok() && inp_query.fail_on_no_match) {
        log("  No device matched the query: {}", to_string(inp_query));
        return false;
    }
    return finalize_device(*this, best, inp_query.caps);
} catch (...) {
    log("  Exception while matching the query.");
    return false;
}

bool basic_uinput::init(device_query const& inp_query) noexcept {
    // don't re-initialize
    if (is_ok()) {
        return true;
    }
    return set_device_from(inp_query);
}

bool basic_uinput::operator()(dev_caps_view const caps_view, start_tag) noexcept {
    return init(caps_view);
}

bool basic_uinput::operator()(device_query const& inp_query, start_tag) noexcept {
    return init(inp_query);
}

fs8::context_action basic_uinput::operator()(event_type const& event) noexcept {
    using enum context_action;
    // log("{}: {} {} {}", devnode(), event.type_name(), event.code_name(), event.value());
    if (!emit(event)) [[unlikely]] {
        return ignore_event;
    }
    return next;
}
