#include "cli_args.hxx"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <libevdev/libevdev.h>
#include <poll.h>
#include <print>
#include <ranges>
#include <string>
#include <unistd.h>

import fs8;
import fs8.devices.queries;
import fs8.devices.udev;
import fs8.lib.evtest;

namespace {
    void print_evtest_header(fs8::evdev const& dev) {
        auto const* raw = dev.device_ptr();
        std::println("Input driver version is 1.0.1");
        std::println("Input device ID: bus 0x{:x} vendor 0x{:x} product 0x{:x} version 0x{:x}",
                     libevdev_get_id_bustype(raw),
                     libevdev_get_id_vendor(raw),
                     libevdev_get_id_product(raw),
                     libevdev_get_id_version(raw));
        std::println("Input device name: \"{}\"", dev.device_name());
        std::println("Supported events:");

        for (unsigned type = 0; type <= EV_MAX; ++type) {
            if (!dev.has_event_type(static_cast<fs8::event_type::type_type>(type))) {
                continue;
            }
            auto const* tname = libevdev_event_type_get_name(type);
            std::println("  Event type {} ({})", type, tname != nullptr ? tname : "<unknown>");

            auto const code_max = fs8::event_type_max_code(type);

            for (unsigned code = 0; code <= code_max; ++code) {
                if (!dev.has_event_code(static_cast<fs8::event_type::type_type>(type), static_cast<fs8::event_type::code_type>(code))) {
                    continue;
                }
                auto const* cname = libevdev_event_code_get_name(type, code);
                std::println("    Event code {} ({})", code, cname != nullptr ? cname : "<unknown>");
            }
        }

        std::println("Key repeat handling:");
        std::println("  Repeat type 20 (EV_REP)");
        if (dev.has_event_code(EV_REP, REP_DELAY)) {
            int delay  = 0;
            int period = 0;
            libevdev_get_repeat(raw, &delay, &period);
            std::println("    Repeat code 0 (REP_DELAY)");
            std::println("      Value   {}", delay);
            std::println("    Repeat code 1 (REP_PERIOD)");
            std::println("      Value   {}", period);
        }
        std::println("Properties:");
        std::println("Testing ... (interrupt to exit)");
    }
} // namespace

int run_evtest(options const& opts) {
    // --- open the device ---
    fs8::evdev dev;
    if (opts.queries.empty()) {
        struct DevEntry {
            std::string devnode;
            std::string name;
            int         event_num = -1;
        };

        std::vector<DevEntry> devices;
        fs8::udev_enumerate   enumerate{};
        enumerate.match_subsystem("input");
        enumerate.match_sysname("event*");
        enumerate.scan_devices();

        for (auto const& entry : enumerate.list_entries()) {
            fs8::udev_device udev_dev{entry};
            auto const       dn = udev_dev.devnode();
            if (dn.empty()) {
                continue;
            }

            auto const sn        = udev_dev.sysname();
            int        num       = -1;
            auto const [ptr, ec] = std::from_chars(sn.data() + 5, sn.data() + sn.size(), num);
            if (ec != std::errc{}) {
                continue;
            }

            fs8::evdev d{std::filesystem::path{dn}};
            if (!d.is_ok()) {
                continue;
            }
            devices.emplace_back(DevEntry{
              .devnode   = std::string{dn},
              .name      = std::string{d.device_name()},
              .event_num = num,
            });
        }

        if (devices.empty()) [[unlikely]] {
            std::println(stderr, "No devices available");
            return EXIT_FAILURE;
        }

        std::ranges::sort(devices, {}, &DevEntry::event_num);

        std::println("No device specified, trying to scan all of /dev/input/event*");
        if (getuid() != 0) {
            std::println("Not running as root, no devices may be available.");
        }
        std::println("Available devices:");
        for (std::size_t i = 0; i < devices.size(); ++i) {
            std::println("{}:\t{}", devices[i].devnode, devices[i].name);
        }

        std::print("Select the device event number [0-{}]: ", devices.back().event_num);
        // NOLINTNEXTLINE(*-value-used-after-return)
        std::fflush(stdout);

        int selection = -1;
        if (!(std::cin >> selection)) [[unlikely]] {
            std::println(stderr, "Selection failure.");
            return EXIT_FAILURE;
        }
        auto const it = std::ranges::find_if(devices, [selection](auto const& d) {
            return d.event_num == selection;
        });
        if (it == devices.end()) [[unlikely]] {
            std::println(stderr, "Invalid selection.");
            return EXIT_FAILURE;
        }

        dev = fs8::evdev{std::filesystem::path{it->devnode}};
    } else {
        auto oq = fs8::owned_query{opts.queries.front()};
        dev     = fs8::device(oq);
    }

    if (!dev.is_ok()) [[unlikely]] {
        std::println(stderr, "Could not open device.");
        return EXIT_FAILURE;
    }

    print_evtest_header(dev);

    if (opts.grab) {
        dev.grab_input(true);
    }

    char      fmt_buf[fs8::evtest_format_buf_size];
    int const fd  = dev.native_handle();
    pollfd    pfd = {.fd = fd, .events = POLLIN, .revents = 0};

    while (signals::sig == 0) {
        int ready = ::poll(&pfd, 1, -1);
        // NOLINTBEGIN(*-loop-convert)
        while (ready < 0 && errno == EINTR && signals::sig == 0) {
            ready = ::poll(&pfd, 1, -1);
        }
        // NOLINTEND(*-loop-convert)

        if (signals::sig != 0 || ready < 0) [[unlikely]] {
            break;
        }

        // NOLINTNEXTLINE(*-signed-*)
        if ((static_cast<unsigned>(pfd.revents) & (POLLHUP | POLLERR)) != 0u) [[unlikely]] {
            break;
        }

        while (signals::sig == 0) {
            auto const ev = dev.next();
            if (!ev.has_value()) [[unlikely]] {
                break;
            }

            fs8::event_type            event{*ev};
            fs8::default_evtest_format fmt;
            auto const                 text = fmt.format(event, fmt_buf);
            if (!text.empty()) [[likely]] {
                // NOLINTNEXTLINE(*-unused-return-value)
                auto const n = write(STDOUT_FILENO, text.data(), text.size());
                (void) n;
            }
        }
    }

    return EXIT_SUCCESS;
}
