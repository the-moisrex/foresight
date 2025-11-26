#include <algorithm>
#include <csignal>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <print>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>
import foresight.main;
import foresight.mods.intercept;
import foresight.devices.evdev;
import foresight.devices.uinput;
import foresight.mods.context;
import foresight.mods.stopper;
import foresight.mods.inout;
import foresight.devices.uinput;
import foresight.main.utils;
import foresight.main.systemd;

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
        } action = action_type::none;

        /// intercept file
        std::vector<fs8::input_file_type> files;

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

    void print_help() {
        std::println("{}", R"TEXT(Usage: foresight [options] [action]
  Arguments:
    -h | --help                   Print help.

  Actions:
    intercept [files...]          Intercept the files and print everything to stdout.
       -g | --grab                Grab the input.
                                  Stops everyone else from using the input.
                                  Only use this if you know what you're doing!

    redirect [files...]           Redirect stdin to the specified files.
    to       [files...]           Alias for 'redirect'
    systemd  exec-file [args...]  Install exec-file as a user service to systemd.
    list-devices                  List input devices

    help                 Print help.

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
        for (auto const& dev : fs8::all_input_devices()) {
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
        std::println(
          "{: <{}}  {: <{}}  {: <{}}",
          "Device",
          w_name,
          "Physical Location",
          w_loc,
          "Unique ID",
          w_id);

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
        } else if (action_str == "redirect" || action_str == "to") {
            set_action(opts, redirect);
        } else if (action_str == "systemd") {
            set_action(opts, systemd);
            return opts;
        } else if (action_str == "list-devices") {
            set_action(opts, list_devices);
            return opts;
        }

        bool grab = false;
        for (std::size_t index = 2; index < argv.size(); ++index) {
            std::string_view const opt{argv[index]};

            if (opt == "--help" || opt == "-h") {
                set_action(opts, help);
            }
            if (opt == "--grab" || opt == "-g") {
                grab = true;
                continue;
            }

            switch (opts.action) {
                case intercept: {
                    opts.files.emplace_back(opt, grab);
                    if (auto const status = std::filesystem::status(opts.files.back().file); !exists(status))
                    {
                        throw invalid_argument(
                          format("File does not exist: {}", opts.files.back().file.string()));
                    } else if (!is_character_file(status)) { // NOLINT(*-else-after-return)
                        throw invalid_argument(
                          format("It's not a file: {}", opts.files.back().file.string()));
                    }
                    break;
                }

                case redirect: {
                    opts.files.emplace_back(opt);
                    if (auto const status = std::filesystem::status(opts.files.back().file); !exists(status))
                    {
                        throw invalid_argument(
                          format("File does not exist: {}", opts.files.back().file.string()));
                    } else if (!is_character_file(status)) { // NOLINT(*-else-after-return)
                        throw invalid_argument(
                          format("It's not a file: {}", opts.files.back().file.string()));
                    }
                    break;
                }

                default: {
                    throw invalid_argument(format("Invalid argument {}", opt));
                }
            }
        }

        switch (opts.action) {
            case intercept:
                if (opts.files.empty()) {
                    throw invalid_argument("Please provide /dev/input/eventX file as an argument.");
                }
                break;
            default: break;
        }

        return opts;
    }

    inline namespace signals {
        // NOLINTBEGIN(*-avoid-non-const-global-variables)
        std::sig_atomic_t volatile sig;
        std::vector<std::function<void(std::sig_atomic_t)>> actions{};

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

    int run_action(options const& opts) {
        using enum options::action_type;
        switch (opts.action) {
            case none:
            case help: {
                print_help();
                return EXIT_FAILURE;
            }
            case intercept: {
                static constinit auto pipeline = fs8::context | fs8::stopper | fs8::intercept | fs8::output;

                auto& sig_stopper = pipeline.mod(fs8::stopper);
                auto& inpor       = pipeline.mod(fs8::intercept);

                register_stop_signal(sig_stopper);
                inpor.set_files(opts.files);

                pipeline();
                return EXIT_SUCCESS;
            }
            case redirect: {
                if (opts.files.size() != 1) {
                    throw std::invalid_argument("Only pass one file for redirect.");
                }

                static constinit auto pipeline = fs8::context | fs8::stopper | fs8::input | fs8::uinput;

                auto& out         = pipeline.mod(fs8::uinput);
                auto& sig_stopper = pipeline.mod(fs8::stopper);

                auto const&      file = opts.files.front().file;
                fs8::evdev const dev{file};
                if (!dev.ok()) {
                    throw std::runtime_error(
                      std::format("Could not open device to write into {}", file.string()));
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
            default: {
                fs8::keyboard kbd;
                return kbd.loop();
            }
        }
        // std::unreachable();
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
} catch (std::system_error const& err) {
    std::println(stderr, "System Error ({} {}): {}", err.code().value(), err.code().message(), err.what());
} catch (std::domain_error const& err) {
    std::println(stderr, "Domain Error: {}", err.what());
} catch (std::exception const& err) {
    std::println(stderr, "Fatal exception: {}", err.what());
} catch (...) {
    std::println(stderr, "Fatal unknown exception.");
}
