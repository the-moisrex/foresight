#include <algorithm>
#include <coroutine>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <libevdev/libevdev.h>
#include <poll.h>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>
import fs8;
import fs8.mods;
import fs8.devices.evdev;
import fs8.devices.udev;
import fs8.devices.queries;
import fs8.context;
import fs8.lib.evtest;
import fs8.lib.xkb.how2type;
import fs8.lib.xkb;
import fs8.utils;
import fs8.systemd;
import fs8.scaffold;

namespace {

    /// Holds all the user options for everything situation that this software can handle
    struct options {
        enum struct action_type : std::uint8_t {
            none = 0,
            help,
            intercept,
            redirect,
            systemd,
            list_devices,
            how_to_type,
            new_app,
            matches,
            evtest,
            live,
            version,
        } action = action_type::none;

        /// intercept/redirect query
        std::vector<fs8::owned_query> queries;

        /// matches patterns
        std::vector<std::string_view> patterns;

        /// Echo the triggering events for `matches`
        bool echo_events = false;

        /// Grab the device (intercept/evtest)
        bool grab = false;

        /// All args
        std::span<char const* const> args;
    };

    void set_action(options& opt, options::action_type const inp_action) {
        if (opt.action == inp_action) {
            return;
        }
        if (opt.action != options::action_type::none) {
            throw std::invalid_argument(std::format("Invalid argument syntax, two actions provided."));
        }
        opt.action = inp_action;
    }

    void print_version() {
#ifdef FORESIGHT_VERSION
        std::println("foresight {}", FORESIGHT_VERSION);
#else
        std::println("foresight (unknown version)");
#endif
    }

    void print_help() {
        std::println("{}", R"TEXT(Usage: foresight [options] [action]
  Arguments:
    -h | --help                   Print help.
    -v | --version                Print version.

  Actions:
    intercept [queries...]        Intercept the devices matching the queries and
                                  print everything to stdout.
       -g | --grab                Grab the input.
                                  Stops everyone else from using the input.
                                  Only use this if you know what you're doing!

    redirect [query]              Redirect stdin to the device matching the query.
    to       [query]              Alias for 'redirect'
    systemd  exec-file [args...]  Install exec-file as a user service to systemd.
    list-devices                  List input devices
    how-to-type [--evtest] [str]  How to type the specified input?
    new       [name] [template]   Create a new app from a template.
                                  Use 'foresight new --list-templates' to see them.
    matches   [pattern...]        Match key combos in an evtest-format stream
                                   read from stdin, printing 'Matched <pattern>'
                                   when one is detected.
       --echo-events              Also echo the triggering event line.

    evtest   [device]             Like the evtest command: list devices and let
                                    the user select one, or open the specified
                                    device. Print device info and events in
                                    evtest text format to stdout.
       -g | --grab                Grab the input exclusively.

    live     [device]             Live view: compact, aligned event display with
                                    mouse accumulation, keyboard text, and hold
                                    durations. Uses terminal colors when interactive.
       -g | --grab                Grab the input exclusively.

    help                 Print help.

  Queries are device names, paths (e.g. /dev/input/event1), or udev terms
  (e.g. "name=event0", "attr:device/name=My Mouse", "keyboard").

  Example Usages:
    $ keyboard=/dev/input/event1
    $ foresight intercept -g $keyboard | x2y | foresight redirect $keyboard
      --------------------------------   ---   ----------------------------
        |                                 |      |
        |                                 |      |
        |                                 |      |
        |                                 |      |
        `----> Intercept the input        |      `---> put input back to device
                                         /
                                        /
                                       /
             --------------------------
            /
    $ cat discard-fast-clicks.c  # you can do it with any programming language you like
      #include <stdio.h>
      #include <stdlib.h>
      #include <linux/input.h>

      int main(void) {
          setbuf(stdin, NULL);   // disable stdin buffer
          setbuf(stdout, NULL);  // disable stdout buffer

          struct input_event event;

          // read from the input
          while (fread(&event, sizeof(event), 1, stdin) == 1) {

              // modify the input however you like
              // here, we change "x" to "y"
              if (event.type == EV_KEY && event.code == KEY_X)
                  event.code = KEY_Y;

              // write it to stdout
              fwrite(&event, sizeof(event), 1, stdout);
          }
      }

    $ foresight how-to-type --evtest "[Ctrl+Shift+Left]" \
      | foresight matches "[ctrl+shift+left]"
      Matched [ctrl+shift+left] at 0.000000

)TEXT");
    }

    void print_new_help() {
        std::println("{}", R"TEXT(Usage: foresight new [name] [template]
       foresight new --list-templates

Creates a new Foresight app from a template, in the current directory or at
the given path (whose filename becomes the app name).

Positionals (interchangeable):
    name                  The app name/path; e.g. "my-app" or "subdir/my-app".
    template              The template to use; e.g. "basic", "x2y", "auto-typer".
                          Omitted, defaults to "basic". An argument matching a
                          known template is treated as the template.

Options:
    -h | --help           Print this help.
    --list-templates      List the available templates.

)TEXT");
    }

    void print_input_devices_table() {
        struct Entry {
            std::string name;
            std::string location;
            std::string id;
        };

        std::vector<Entry> devices;
        devices.reserve(16);

        // Minimum column widths (length of header texts)
        size_t w_name = 6;  // "Device"
        size_t w_loc  = 17; // "Physical Location"
        size_t w_id   = 9;  // "Unique ID"

        // Single pass: measure + store owned strings
        // Enumerate input devices through the query system (udev), then open
        // each evdev to read name/location/unique-id fields.
        for (auto pick : fs8::filter_devices(fs8::input)) {
            auto dev = fs8::to_evdev(pick);
            if (!dev.is_ok()) [[unlikely]] {
                continue;
            }
            auto const name_sv = dev.device_name();
            auto const loc_sv  = dev.physical_location();
            auto const id_sv   = dev.unique_identifier();

            w_name = std::max(w_name, name_sv.size());
            w_loc  = std::max(w_loc, loc_sv.size());
            w_id   = std::max(w_id, id_sv.size());

            devices.emplace_back(std::string{name_sv}, std::string{loc_sv}, std::string{id_sv});
        }

        if (devices.empty()) {
            std::println("No input devices found.");
            return;
        }

        // Header (still uses println; widths are constant here, so it compiles)
        std::println("{: <{}}  {: <{}}  {: <{}}", "Device", w_name, "Physical Location", w_loc, "Unique ID", w_id);

        // Separator
        std::println("{:-<{}}  {:-<{}}  {:-<{}}", "", w_name, "", w_loc, "", w_id);

        // Rows
        for (auto const& [name, location, id] : devices) {
            std::println("{: <{}}  {: <{}}  {: <{}}", name, w_name, location, w_loc, id, w_id);
        }

        // Footer
        std::println("\n{} device{} detected.", devices.size(), devices.size() == 1 ? "" : "s");
    }

    options parse_arguments(std::span<char const* const> const argv) {
        using enum options::action_type;
        using std::format;
        using std::invalid_argument;

        options opts{};
        if (argv.size() <= 1) {
            return opts;
        }
        opts.args = argv;


        // NOLINTNEXTLINE(*-pro-bounds-pointer-arithmetic)
        if (std::string_view const action_str{argv[1]}; action_str == "intercept") {
            set_action(opts, intercept);
        } else if (action_str == "help") {
            set_action(opts, help);
        } else if (action_str == "--version" || action_str == "-v") {
            set_action(opts, version);
            return opts;
        } else if (action_str == "redirect" || action_str == "to") {
            set_action(opts, redirect);
        } else if (action_str == "systemd") {
            set_action(opts, systemd);
            return opts;
        } else if (action_str == "list-devices") {
            set_action(opts, list_devices);
            return opts;
        } else if (action_str == "how-to-type" || action_str == "how2type") {
            set_action(opts, how_to_type);
            return opts;
        } else if (action_str == "new") {
            set_action(opts, new_app);
            return opts;
        } else if (action_str == "matches") {
            set_action(opts, matches);
        } else if (action_str == "evtest") {
            set_action(opts, evtest);
        } else if (action_str == "live") {
            set_action(opts, live);
        }

        bool grab = false;
        for (std::size_t index = 2; index < argv.size(); ++index) {
            std::string_view const opt{argv[index]};

            if (opt == "--help" || opt == "-h") {
                opts.action = help;
                continue;
            }
            if (opt == "--version" || opt == "-v") {
                opts.action = version;
                continue;
            }
            if (opt == "--grab" || opt == "-g") {
                grab      = true;
                opts.grab = true;
                continue;
            }
            if (opt == "--echo-events") {
                opts.echo_events = true;
                continue;
            }

            switch (opts.action) {
                case intercept:
                case redirect: {
                    opts.queries.emplace_back(opt);
                    opts.queries.back().grab = grab;
                    break;
                }

                case evtest:
                case live: {
                    opts.queries.emplace_back(opt);
                    break;
                }

                case matches: {
                    opts.patterns.emplace_back(opt);
                    break;
                }

                default: {
                    throw invalid_argument(format("Invalid argument {}", opt));
                }
            }
        }

        switch (opts.action) {
            case intercept:
                if (opts.queries.empty()) {
                    throw invalid_argument("Please provide a device query as an argument.");
                }
                break;
            case redirect:
                if (opts.queries.size() != 1) {
                    throw invalid_argument("Only pass one query for redirect.");
                }
                break;
            case matches:
                if (opts.patterns.empty()) {
                    throw invalid_argument("Please provide a pattern as an argument.");
                }
                break;
            default: break;
        }

        return opts;
    }

    int create_new_app(std::span<char const* const> const args) {
        using enum options::action_type;

        bool             list_templates = false;
        bool             help_requested = false;
        std::string_view tpl;
        std::string_view name;

        for (auto const arg : args | std::views::drop(2)) { // remove "foresight new"
            std::string_view const cur{arg};
            if (cur == "--list-templates") {
                list_templates = true;
                continue;
            }
            if (cur == "--help" || cur == "-h") {
                help_requested = true;
                continue;
            }
            if (fs8::is_valid_template(cur)) {
                if (!tpl.empty()) {
                    throw std::invalid_argument(
                      std::format("'{}' and '{}' are both templates; pass one template and one app name.", tpl, cur));
                }
                tpl = cur;
                continue;
            }
            if (!name.empty()) {
                throw std::invalid_argument(std::format("Unknown argument '{}'.", cur));
            }
            name = cur;
        }

        if (list_templates) {
            std::println("Available templates:");
            for (auto const& templ : fs8::available_templates()) {
                std::println("  {:<12} {}", templ.name, templ.description);
            }
            return EXIT_SUCCESS;
        }
        if (help_requested) {
            print_new_help();
            return EXIT_SUCCESS;
        }
        if (name.empty()) {
            if (tpl.empty()) {
                throw std::invalid_argument("Please provide a name for the app.");
            }
            name = tpl; // e.g. `foresight new x2y` -> an app named after the template
        }
        if (tpl.empty()) {
            tpl = "basic";
        }

        fs8::create_app(name, tpl);

        std::println();
        std::println("Next steps:");
        std::println("  cd {}", name);
        std::println("  cmake --preset release");
        std::println("  cmake --build --preset release");
        return EXIT_SUCCESS;
    }

    inline namespace signals {
        // NOLINTBEGIN(*-avoid-non-const-global-variables)
        std::sig_atomic_t volatile sig;
        std::vector<std::move_only_function<void(std::sig_atomic_t) const>> actions{};

        // NOLINTEND(*-avoid-non-const-global-variables)

        template <typename T>
        void register_stop_signal(T& obj) {
            actions.emplace_back([&obj](std::sig_atomic_t const cur_sig) {
                switch (cur_sig) {
                    case SIGINT:
                    case SIGKILL:
                    case SIGTERM: obj.stop(); break;
                    default: break;
                }
            });
        }
    } // namespace signals

    void handle_signals(int const signal) {
        // let's not care about race conditions here, shall we?
        // I like to live dangerously here in `foresight` land.
        sig = signal;
        for (auto const& func : actions) {
            func(sig);
        }
    }

    int run_matches(std::span<std::string_view const> const patterns, bool const echo_events) {
        using std::println;

        // Reuse the same search engine the `typed` mod uses, so patterns behave
        // identically to library pipelines: `<...>`/`[...]`/`<<...>>`/`[[...]]`
        // and plain text.
        fs8::basic_search_engine    engine;
        std::vector<std::uint16_t>  trigger_ids;
        std::vector<fs8::aho_state> states;
        trigger_ids.reserve(patterns.size());
        states.reserve(patterns.size());
        for (auto const& pattern : patterns) {
            trigger_ids.emplace_back(engine.emplace_pattern(pattern));
            states.emplace_back(fs8::aho_state{0u});
        }

        fs8::xkb::basic_state keyboard_state;
        keyboard_state.initialize(fs8::xkb::get_default_keymap());

        bool        matched_any = false;
        std::string line;
        while (std::getline(std::cin, line)) {
            fs8::parsed_evtest_event parsed;
            if (!fs8::parse_evtest_line(line, parsed)) {
                continue;
            }
            fs8::event_type const event{parsed.event};
            for (std::size_t index = 0; index < trigger_ids.size(); ++index) {
                if (engine.search(event, trigger_ids[index], keyboard_state, states[index])) {
                    if (echo_events) {
                        println("{}", line);
                    }
                    println("Matched {} at {:.6f}", patterns[index], parsed.time);
                    matched_any = true;
                }
            }
        }

        return matched_any ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /// Print evtest-style device header matching real evtest output.
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
            std::println("  Event type {} ({})", type, tname ? tname : "<unknown>");

            auto const code_max = fs8::event_type_max_code(type);

            for (unsigned code = 0; code <= code_max; ++code) {
                if (!dev.has_event_code(static_cast<fs8::event_type::type_type>(type), static_cast<fs8::event_type::code_type>(code))) {
                    continue;
                }
                auto const* cname = libevdev_event_code_get_name(type, code);
                std::println("    Event code {} ({})", code, cname ? cname : "<unknown>");
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

    int run_evtest(options const& opts) {
        // --- open the device ---
        fs8::evdev dev;
        if (opts.queries.empty()) {
            // No query given — enumerate all event* devices via udev (no ID_INPUT filter).
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

                // Extract event number from sysname (e.g. "event10").
                auto const sn        = udev_dev.sysname();
                int        num       = -1;
                auto const [ptr, ec] = std::from_chars(sn.data() + 5, sn.data() + sn.size(), num);
                if (ec != std::errc{}) {
                    continue;
                }

                // Open with libevdev to read the device name.
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

            // Sort by event number so the list matches what evtest shows.
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
                std::println(stderr, "Selection failure.");
                return EXIT_FAILURE;
            }
            // Find the device with the matching event number.
            auto const it = std::ranges::find_if(devices, [selection](auto const& d) {
                return d.event_num == selection;
            });
            if (it == devices.end()) {
                std::println(stderr, "Invalid selection.");
                return EXIT_FAILURE;
            }

            dev = fs8::evdev{std::filesystem::path{it->devnode}};
        } else {
            dev = fs8::device(opts.queries.front());
        }

        if (!dev.is_ok()) {
            std::println(stderr, "Could not open device.");
            return EXIT_FAILURE;
        }

        // --- print the header ---
        print_evtest_header(dev);

        // --- grab ---
        if (opts.grab) {
            dev.grab_input(true);
        }

        // --- event loop ---
        fs8::default_evtest_format fmt;
        char                       fmt_buf[fs8::evtest_format_buf_size];
        int const                  fd  = dev.native_handle();
        pollfd                     pfd = {.fd = fd, .events = POLLIN, .revents = 0};

        while (signals::sig == 0) {
            int ready = 0;
            do {
                ready = ::poll(&pfd, 1, -1);
            } while (ready < 0 && errno == EINTR && signals::sig == 0);

            if (signals::sig != 0 || ready < 0) {
                break;
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
                auto const      text = fmt.format(event, fmt_buf);
                if (!text.empty()) {
                    // Write the formatted event line to stdout.
                    auto const n = write(STDOUT_FILENO, text.data(), text.size());
                    (void) n;
                }
            }
        }

        return EXIT_SUCCESS;
    }

    int run_live(options const& opts) {
        // --- open the device ---
        fs8::evdev dev;
        if (opts.queries.empty()) {
            // No query given — enumerate all event* devices via udev.
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
            dev = fs8::device(opts.queries.front());
        }

        if (!dev.is_ok()) {
            std::println(stderr, "Could not open device.");
            return EXIT_FAILURE;
        }

        // --- print the header ---
        std::println("Live view — {} — interrupt to exit", dev.device_name());

        // --- grab ---
        if (opts.grab) {
            dev.grab_input(true);
        }

        // --- event loop ---
        bool const     is_terminal = isatty(STDOUT_FILENO) == 1;
        fs8::live_view lv{is_terminal};
        lv.set_ansi(is_terminal);

        int const fd  = dev.native_handle();
        pollfd    pfd = {.fd = fd, .events = POLLIN, .revents = 0};

        while (signals::sig == 0) {
            int ready = 0;
            do {
                ready = ::poll(&pfd, 1, 100); // 100ms timeout for live flush
            } while (ready < 0 && errno == EINTR && signals::sig == 0);

            if (signals::sig != 0) {
                break;
            }

            if (ready == 0) {
                // Timeout — flush accumulated state
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

        // Flush any remaining accumulated state on exit
        lv.flush(STDOUT_FILENO);

        return EXIT_SUCCESS;
    }

    int run_action(options const& opts) {
        using enum options::action_type;
        switch (opts.action) {
            case none:
            case help: {
                print_help();
                return EXIT_FAILURE;
            }
            case version: {
                print_version();
                return EXIT_SUCCESS;
            }
            case intercept: {
                static constinit auto pipeline =
                  fs8::context | fs8::io_manager | fs8::intercept | fs8::input_manager | fs8::stopper | fs8::std_output;

                auto& sig_stopper = pipeline.mod(fs8::stopper);
                auto& inpor       = pipeline.mod(fs8::intercept);

                register_stop_signal(sig_stopper);
                for (auto const& q : opts.queries) {
                    inpor.add(q);
                }

                pipeline();
                return EXIT_SUCCESS;
            }
            case redirect: {
                if (opts.queries.size() != 1) {
                    throw std::invalid_argument("Only pass one query for redirect.");
                }

                static constinit auto pipeline = fs8::context | fs8::stopper | fs8::from_input | fs8::uinput;

                auto& out         = pipeline.mod(fs8::uinput);
                auto& sig_stopper = pipeline.mod(fs8::stopper);

                fs8::evdev const dev = fs8::device(opts.queries.front());
                if (!dev.is_ok()) {
                    throw std::runtime_error("Could not open device for the given query.");
                }
                out.set_device(dev);
                register_stop_signal(sig_stopper);

                pipeline();

                return EXIT_SUCCESS;
            }
            case systemd: {
                fs8::systemd_service service{};
                service.description("Foresight Input Modifier");
                auto const args =
                  opts.args
                  | std::views::drop(2) // removing "foresight systemd"
                  | fs8::transform_to<std::string_view>()
                  | std::ranges::to<std::vector>();
                service.execStart(args);
                std::println("Installing as a systemd service...");
                service.install();
                service.enable();
                return EXIT_SUCCESS;
            }
            case list_devices: {
                print_input_devices_table();
                return EXIT_SUCCESS;
            }
            case how_to_type: {
                using enum fs8::xkb::how2type::output_syntax;
                auto const args =
                  opts.args
                  | std::views::drop(2) // remove "foresight how-to-type"
                  | fs8::transform_to<std::string_view>()
                  | std::ranges::to<std::vector>();
                if (args.empty()) [[unlikely]] {
                    std::println(stderr, "No input specified.");
                    return EXIT_FAILURE;
                }
                bool const                              evtest_syntax = std::ranges::any_of(args, [](std::string_view const str) {
                    return str == "--evtest";
                });
                fs8::xkb::how2type::output_syntax const syntax        = evtest_syntax ? evtest : cpp_code;
                for (auto const str : args) {
                    if (str == "--evtest" || str == "--cpp") {
                        continue;
                    }
                    fs8::xkb::how2type::print(str, syntax);
                }
                return EXIT_SUCCESS;
            }
            case new_app: {
                return create_new_app(opts.args);
            }
            case matches: {
                return run_matches(opts.patterns, opts.echo_events);
            }
            case evtest: {
                return run_evtest(opts);
            }
            case live: {
                return run_live(opts);
            }
            default: {
                fs8::keyboard_runner kbd;
                return kbd.loop();
            }
        }
        std::unreachable();
    }

} // namespace

int main(int const argc, char const* const* argv) try {
    std::ignore = std::signal(SIGINT, handle_signals);
    std::ignore = std::signal(SIGTERM, handle_signals);
    std::ignore = std::signal(SIGKILL, handle_signals);

    auto const opts = parse_arguments(std::span{argv, argv + argc});
    return run_action(opts);
} catch (std::invalid_argument const& err) {
    std::println(stderr, "{}", err.what());
    return EXIT_FAILURE;
} catch (std::system_error const& err) {
    std::println(stderr, "System Error ({} {}): {}", err.code().value(), err.code().message(), err.what());
    return EXIT_FAILURE;
} catch (std::domain_error const& err) {
    std::println(stderr, "Domain Error: {}", err.what());
    return EXIT_FAILURE;
} catch (std::exception const& err) {
    std::println(stderr, "Fatal exception: {}", err.what());
    return EXIT_FAILURE;
} catch (...) {
    std::println(stderr, "Fatal unknown exception.");
    return EXIT_FAILURE;
}
