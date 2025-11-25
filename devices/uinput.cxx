// Created by moisrex on 6/29/24.

module;
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
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
module foresight.devices.uinput;
import foresight.mods.event;
import foresight.main.log;

using foresight::basic_uinput;


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

basic_uinput::basic_uinput(evdev const& evdev_dev, std::filesystem::path const& file) noexcept
  : basic_uinput(evdev_dev.device_ptr(), file) {}

basic_uinput::basic_uinput(evdev const& evdev_dev, int const file_descriptor) noexcept
  : basic_uinput(evdev_dev.device_ptr(), file_descriptor) {}

basic_uinput::basic_uinput(libevdev const* evdev_dev, std::filesystem::path const& file) noexcept {
    auto const file_descriptor = ::open(file.c_str(), O_RDWR | O_NONBLOCK);
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

        rc = ioctl(uinput_dev->fd,
                   UI_GET_SYSNAME(sizeof(buf) - strlen(SYS_INPUT_DIR)),
                   &buf[strlen(SYS_INPUT_DIR)]);
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
            if (fstat(fd, &st)
                == -1
                || st.st_ctime
                < uinput_dev->ctime[0]
                || st.st_ctime
                > uinput_dev->ctime[1])
            {
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
                    foresight::log("multiple identical devices found. syspath is unreliable");
                    break;
                }
                rc = snprintf(buf, sizeof(buf), "%s%s", SYS_INPUT_DIR, namelist[i]->d_name);
                if (rc < 0 || static_cast<size_t>(rc) >= sizeof(buf)) {
                    foresight::log("Invalid syspath, syspath is unreliable");
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
        setup.id.bustype     = 0;
        setup.id.version     = 0;
        setup.ff_effects_max = 0;

        if (ioctl(fd, UI_DEV_SETUP, &setup) == 0) {
            errno = 0;
        }
        return errno;
    }

} // namespace

void basic_uinput::set_device(int fd, std::string_view const name) noexcept {
    close();

    bool const close_fd_on_error = fd == LIBEVDEV_UINPUT_OPEN_MANAGED;

    my_libevdev_uinput* mdev = alloc_uinput_device(name.data());
    if (mdev == nullptr) {
        err_code = static_cast<std::errc>(ENOMEM);
        return;
    }

    if (fd == LIBEVDEV_UINPUT_OPEN_MANAGED) {
        fd = ::open("/dev/uinput", O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            err_code = static_cast<std::errc>(errno);
            return;
        }
        mdev->fd_is_managed = 1;
    } else if (fd < 0) {
        log("Invalid fd {}", fd);
        errno    = EBADF;
        err_code = static_cast<std::errc>(errno);
        return;
    }

    if (unsigned int uinput_version = 0;
        ::ioctl(fd, UI_GET_VERSION, &uinput_version) == 0 && uinput_version < 5U)
    {
        log("Kernel needs to supports uinput version 5, but it doesn't now.");
        return;
    }

    if (uinput_SETUP(fd, mdev) != 0) {
        err_code = static_cast<std::errc>(errno);
        libevdev_uinput_destroy(reinterpret_cast<libevdev_uinput*>(mdev));
        if (fd != -1 && close_fd_on_error) {
            ::close(fd);
        }
        return;
    }

    /* ctime notes time before/after ioctl to help us filter out devices
       when traversing /sys/devices/virtual/input to find the device
       node.

       this is in seconds, so ctime[0]/[1] will almost always be
       identical but /sys doesn't give us sub-second ctime so...
     */
    mdev->ctime[0] = time(nullptr);

    if (::ioctl(fd, UI_DEV_CREATE, nullptr) == -1) {
        err_code = static_cast<std::errc>(errno);
        libevdev_uinput_destroy(reinterpret_cast<libevdev_uinput*>(mdev));
        if (fd != -1 && close_fd_on_error) {
            ::close(fd);
        }
        return;
    }

    mdev->ctime[1] = time(nullptr);
    mdev->fd       = fd;

    if (fetch_syspath_and_devnode(mdev) == -1) {
        log("Unable to fetch syspath or device node.");
        errno    = ENODEV;
        err_code = static_cast<std::errc>(errno);
        libevdev_uinput_destroy(reinterpret_cast<libevdev_uinput*>(mdev));
        if (fd != -1 && close_fd_on_error) {
            ::close(fd);
        }
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
    if (::ioctl(native_handle(), UI_SET_EVBIT, type) == -1) {
        err_code = static_cast<std::errc>(errno);
    }
}

void basic_uinput::enable_event_code(ev_type const type, code_type const code) noexcept {
    /* uinput can't set EV_REP */
    if (type == EV_REP) {
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
        case EV_SW: uinput_bit = UI_SET_SWBIT; break;
        default:
            errno    = EINVAL;
            err_code = static_cast<std::errc>(errno);
            return;
    }

    if (::ioctl(native_handle(), uinput_bit, code) == -1) {
        err_code = static_cast<std::errc>(errno);
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
        err_code = static_cast<std::errc>(errno);
    }
}

void basic_uinput::apply_caps(dev_caps_view const inp_caps) noexcept {
    using enum caps_action;
    for (auto const& [type, codes, action] : inp_caps) {
        switch (action) {
            case append:
                enable_event_type(type);
                for (auto const code : codes) {
                    enable_event_code(type, code);
                }
                break;
            case remove_codes:
            case remove_type: break;
        }
    }
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
    // don't re-initialize
    if (is_ok()) {
        return;
    }
    set_device_from(caps_view);
}

void basic_uinput::set_device_from(dev_caps_view const caps_view) noexcept {
    using enum caps_action;

    evdev_rank best{};
    for (evdev_rank&& cur : rank_devices(caps_view)) {
        if (cur.score >= best.score) {
            best = std::move(cur);
        }
    }
    if (best.dev.ok()) {
        // best.dev.apply_caps(caps_view);
        std::string new_name;
        new_name += best.dev.device_name();
        new_name += " (Copied)";
        best.dev.device_name(new_name); // this is okay, it's not changing the original device
        for (auto const& [type, codes, action] : caps_view) {
            switch (action) {
                case append: {
                    auto* dev_ptr = best.dev.device_ptr();
                    if (libevdev_has_event_type(dev_ptr, type) == 0) {
                        libevdev_enable_event_type(dev_ptr, type);
                    }
                    for (auto const code : codes) {
                        if (libevdev_has_event_code(dev_ptr, type, code) == 0) {
                            libevdev_enable_event_code(dev_ptr, type, code, nullptr);
                            log("  Enabled: {} {}",
                                libevdev_event_type_get_name(type),
                                libevdev_event_code_get_name(type, code));
                        }
                    }
                    break;
                }
                case remove_codes:
                case remove_type: break;
            }
        }
        this->set_device(best.dev);
    } else {
        this->set_device();
        this->apply_caps(caps_view);
    }
}

void basic_uinput::operator()(dev_caps_view const caps_view, start_tag) noexcept {
    init(caps_view);
}

void basic_uinput::operator()(std::span<evdev const> const devs, start_tag) noexcept {
    if (is_ok()) {
        return;
    }
    for (auto const& cur_dev : devs) {
        // Don't intercept the one that's being grabbed.
        // if (cur_dev.grab() == grab_state::grabbing_by_others) {
        //     continue;
        // }

        set_device(cur_dev);
        break;
    }
}

foresight::context_action basic_uinput::operator()(event_type const& event) noexcept {
    using enum context_action;
    // log("{}: {} {} {}", devnode(), event.type_name(), event.code_name(), event.value());
    if (!emit(event)) [[unlikely]] {
        return ignore_event;
    }
    return next;
}
