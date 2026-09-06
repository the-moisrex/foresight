#include "cli_args.hxx"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <unistd.h>

#include <poll.h>

import fs8;
import fs8.devices.queries;
import fs8.devices.udev;
import fs8.lib.evtest;

int run_live(options const& opts) {
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

        if (devices.empty()) {
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
        std::fflush(stdout);

        int selection = -1;
        if (!(std::cin >> selection)) {
            std::println(stderr, "Could not select.");
            return EXIT_FAILURE;
        }
        auto const it = std::ranges::find_if(devices, [selection](auto const& d) {
            return d.event_num == selection;
        });
        if (it == devices.end()) {
            std::println(stderr, "Invalid selection.");
            return EXIT_FAILURE;
        }

        dev = fs8::evdev{std::filesystem::path{it->devnode}};
    } else {
        auto oq = fs8::owned_query{opts.queries.front()};
        dev = fs8::device(oq);
    }

    if (!dev.is_ok()) {
        std::println(stderr, "Could not open device.");
        return EXIT_FAILURE;
    }

    std::println("Live view — {} — interrupt to exit", dev.device_name());

    if (opts.grab) {
        dev.grab_input(true);
    }

    bool const          is_terminal = isatty(STDOUT_FILENO) == 1;
    fs8::condensed_view lv{is_terminal};
    lv.set_ansi(is_terminal);

    int const fd  = dev.native_handle();
    pollfd    pfd = {.fd = fd, .events = POLLIN, .revents = 0};

    while (signals::sig == 0) {
        int ready = 0;
        do {
            ready = ::poll(&pfd, 1, 100);
        } while (ready < 0 && errno == EINTR && signals::sig == 0);

        if (signals::sig != 0) {
            break;
        }

        if (ready == 0) {
            lv.flush(STDOUT_FILENO);
            continue;
        }

        if (pfd.revents & (POLLHUP | POLLERR)) {
            break;
        }

        while (signals::sig == 0) {
            auto const ev = dev.next();
            if (!ev.has_value()) {
                break;
            }

            fs8::event_type event{*ev};
            event.source(fs8::sid(fs8::intercept));
            lv.process_event(event, STDOUT_FILENO);
        }
    }

    lv.flush(STDOUT_FILENO);

    return EXIT_SUCCESS;
}
