#include <csignal>
#include <exception>
#include <filesystem>
#include <fmt/core.h>
#include <functional>
#include <string_view>
#include <vector>
import foresight.keyboard;
import foresight.intercept;
import foresight.redirect;

namespace {
    struct options {
        // NOLINTBEGIN(*-non-private-member-variables-in-classes)
        enum struct action_type : std::uint8_t {
            none = 0,
            help,
            intercept,
            redirect,
        } action = action_type::none;

        /// intercept file
        std::vector<std::filesystem::path> files;
        bool                               grab = false;

        // NOLINTEND(*-non-private-member-variables-in-classes)

        void set_action(action_type const inp_action) {
            if (action == inp_action) {
                return;
            }
            if (action != action_type::none) {
                throw std::invalid_argument(fmt::format("Invalid argument syntax, two actions provided."));
            }
            action = inp_action;
        }
    };

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
    $ cat x2y.c  # you can do it with any programming language you like
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

    options check_opts(int const argc, char const* const* argv) {
        using enum options::action_type;

        options opts{};
        if (argc <= 1) {
            return opts;
        }


        // NOLINTNEXTLINE(*-pro-bounds-pointer-arithmetic)
        if (std::string_view const action_str{argv[1]}; action_str == "intercept") {
            opts.set_action(intercept);
        } else if (action_str == "help") {
            opts.set_action(help);
        } else if (action_str == "redirect" || action_str == "to") {
            opts.set_action(redirect);
        }

        for (std::size_t index = 2; index < static_cast<std::size_t>(argc); ++index) {
            std::string_view const opt{argv[index]}; // NOLINT(*-pro-bounds-pointer-arithmetic)

            if (opt == "--help" || opt == "-h") {
                opts.set_action(help);
            }

            switch (opts.action) {
                case intercept: {
                    if (opt == "--grab" || opt == "-g") {
                        opts.grab = true;
                        break;
                    }
                    opts.files.emplace_back(opt);
                    if (auto const status = std::filesystem::status(opts.files.back()); !exists(status)) {
                        throw std::invalid_argument(
                          fmt::format("File does not exist: {}", opts.files.back().string()));
                    } else if (!is_character_file(status)) { // NOLINT(*-else-after-return)
                        throw std::invalid_argument(
                          fmt::format("It's not a file: {}", opts.files.back().string()));
                    }
                    break;
                }

                case redirect: {
                    opts.files.emplace_back(opt);
                    if (auto const status = std::filesystem::status(opts.files.back()); !exists(status)) {
                        throw std::invalid_argument(
                          fmt::format("File does not exist: {}", opts.files.back().string()));
                    } else if (!is_character_file(status)) { // NOLINT(*-else-after-return)
                        throw std::invalid_argument(
                          fmt::format("It's not a file: {}", opts.files.back().string()));
                    }
                    break;
                }

                default: {
                    throw std::invalid_argument(fmt::format("Invalid argument {}", opt));
                }
            }
        }

        switch (opts.action) {
            case intercept:
                if (opts.files.empty()) {
                    throw std::invalid_argument("Please provide /dev/input/eventX file as an argument.");
                }
                break;
            default: break;
        }

        return opts;
    }

    namespace {
        // NOLINTBEGIN(*-avoid-non-const-global-variables)
        std::sig_atomic_t volatile sig;
        std::vector<std::function<void(std::sig_atomic_t)>> actions{};
        // NOLINTEND(*-avoid-non-const-global-variables)
    } // namespace

    void handle_signals(int const signal) {
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
                inpor.set_output(stdout);
                if (opts.grab) {
                    inpor.grab_input();
                }
                actions.emplace_back([&inpor](std::sig_atomic_t const sig) {
                    switch (sig) {
                        case SIGINT:
                        case SIGKILL:
                        case SIGTERM: inpor.stop(); break;
                        default: break;
                    }
                });
                return inpor.loop();
            }
            case redirect: {
                redirector rdtor;
                rdtor.set_input(stdin);
                actions.emplace_back([&rdtor](std::sig_atomic_t const sig) {
                    switch (sig) {
                        case SIGINT:
                        case SIGKILL:
                        case SIGTERM: rdtor.stop(); break;
                        default: break;
                    }
                });
                return rdtor.loop();
            }
            default: {
                keyboard kbd;
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

    auto const opts = check_opts(argc, argv);
    return run_action(opts);
} catch (std::invalid_argument const& err) {
    fmt::println(stderr, "{}", err.what());
} catch (std::system_error const& err) {
    fmt::println(stderr, "System Error ({} {}): {}", err.code().value(), err.code().message(), err.what());
} catch (std::domain_error const& err) {
    fmt::println(stderr, "{}", err.what());
} catch (std::exception const& err) {
    fmt::println(stderr, "Fatal exception: {}", err.what());
} catch (...) {
    fmt::println(stderr, "Fatal unknown exception.");
}
