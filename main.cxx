#include <csignal>
#include <exception>
#include <filesystem>
#include <fmt/core.h>
#include <functional>
#include <spdlog/spdlog.h>
#include <string_view>
#include <vector>
import foresight.keyboard;
import foresight.intercept;

struct options {
    // NOLINTBEGIN(*-non-private-member-variables-in-classes)
    enum struct action_type : std::uint8_t {
        none = 0,
        help,
        intercept,
    } action = action_type::none;

    /// intercept file
    std::filesystem::path file;
    bool                  grab = false;

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
    fmt::println("{}",   R"TEXT(Usage: foresight [options] [action]
  arguments:
    -h | --help          Print help.

  actions:
    intercept [files...] Intercept the files and print everything to stdout.
       -g | --grab       Grab the input.
                         Stops everyone else from using the input.
                         Only use this if you know what you're doing!

    redirect [files...]  Redirect stdin to the specified files.

    help                 Print help.

  Example Usages:
    $ export keyboard=/dev/input/event1
    $ foresight intercept $keyboard | x2y | foresight redirect $keyboard
      -----------------------------   ---   ----------------------------
        |                              |      |
        |                              |      |
        |                              |      |
        |                              |      |
        `----> Intercept the input     |      `---> put the modified input back
                                      /
                                     /
                                    /
             -----------------------
            /
    $ cat x2y.c  # you can do it with any programming language you'd like
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
    }

    for (std::size_t index = 2; index < argc; ++index) {
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
                opts.file = opt;
                if (auto const status = std::filesystem::status(opts.file); !exists(status)) {
                    throw std::invalid_argument(fmt::format("File does not exist: {}", opts.file.string()));
                } else if (!is_character_file(status)) {
                    throw std::invalid_argument(fmt::format("It's not a file: {}", opts.file.string()));
                }
                return opts;
            }

            default: {
                throw std::invalid_argument(fmt::format("Invalid argument {}", opt));
            }
        }
    }

    switch (opts.action) {
        case intercept:
            if (opts.file.empty()) {
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
            interceptor inpor{opts.file};
            inpor.set_output(stderr);
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
        default: {
            keyboard kbd;
            return kbd.loop();
        }
    }
    // std::unreachable();
}

int main(int const argc, char const* const* argv) try
{
    std::ignore = std::signal(SIGINT, handle_signals);
    std::ignore = std::signal(SIGTERM, handle_signals);
    std::ignore = std::signal(SIGKILL, handle_signals);

    auto const opts = check_opts(argc, argv);
    return run_action(opts);
} catch (std::invalid_argument const& err) {
    fmt::println(stderr, "{}", err.what());
} catch (std::exception const& err) {
    spdlog::critical("Fatal exception: {}", err.what());
} catch (...) {
    spdlog::critical("Fatal unknown exception.");
}
