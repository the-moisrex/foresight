#include <algorithm>
#include <csignal>
#include <exception>
#include <filesystem>
#include <fmt/core.h>
#include <functional>
#include <span>
#include <string_view>
#include <vector>
import foresight.main;
import foresight.intercept;
import foresight.redirect;
import foresight.evdev;
import foresight.uinput;

namespace {

    /// Holds all the user options for everything situation that this software can handle
    struct options {
        enum struct action_type : std::uint8_t {
            none = 0,
            help,
            intercept,
            redirect,
        } action = action_type::none;

        /// intercept file
        std::vector<input_file_type> files;
    };

    void set_action(options& opt, options::action_type const inp_action) {
        if (opt.action == inp_action) {
            return;
        }
        if (opt.action != options::action_type::none) {
            throw std::invalid_argument(fmt::format("Invalid argument syntax, two actions provided."));
        }
        opt.action = inp_action;
    }

    void print_help() {
        fmt::println("{}", R"TEXT(Usage: foresight [options] [action]
  Arguments:
    -h | --help          Print help.

  Actions:
    intercept [files...] Intercept the files and print everything to stdout.
       -g | --grab       Grab the input.
                         Stops everyone else from using the input.
                         Only use this if you know what you're doing!

    redirect [files...]  Redirect stdin to the specified files.
    to       [files...]  Alias for 'redirect'

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

    options parse_arguments(std::span<char const* const> const argv) {
        using enum options::action_type;
        using fmt::format;
        using std::invalid_argument;

        options opts{};
        if (argv.size() <= 1) {
            return opts;
        }


        // NOLINTNEXTLINE(*-pro-bounds-pointer-arithmetic)
        if (std::string_view const action_str{argv[1]}; action_str == "intercept") {
            set_action(opts, intercept);
        } else if (action_str == "help") {
            set_action(opts, help);
        } else if (action_str == "redirect" || action_str == "to") {
            set_action(opts, redirect);
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
                interceptor inpor{opts.files};
                inpor.set_output(STDOUT_FILENO);
                register_stop_signal(inpor);
                return inpor.loop();
            }
            case redirect: {
                if (opts.files.size() != 1) {
                    throw std::invalid_argument("Only pass one file for redirect.");
                }

                evdev      dev{opts.files.front().file};
                redirector rdtor{dev};

                register_stop_signal(rdtor);
                return rdtor.loop();
            }
            default: {
                foresight::keyboard kbd;
                return kbd.loop();
            }
        }
        // std::unreachable();
    }

} // namespace

int main(int const argc, char const* const* argv) try
{
    std::ignore = std::signal(SIGINT, handle_signals);
    std::ignore = std::signal(SIGTERM, handle_signals);
    std::ignore = std::signal(SIGKILL, handle_signals);

    auto const opts = parse_arguments(std::span{argv, argv + argc});
    return run_action(opts);
} catch (std::invalid_argument const& err) {
    fmt::println(stderr, "{}", err.what());
} catch (std::system_error const& err) {
    fmt::println(stderr, "System Error ({} {}): {}", err.code().value(), err.code().message(), err.what());
} catch (std::domain_error const& err) {
    fmt::println(stderr, "Domain Error: {}", err.what());
} catch (std::exception const& err) {
    fmt::println(stderr, "Fatal exception: {}", err.what());
} catch (...) {
    fmt::println(stderr, "Fatal unknown exception.");
}
