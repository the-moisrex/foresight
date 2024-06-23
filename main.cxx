#include <exception>
#include <filesystem>
#include <fmt/core.h>
#include <spdlog/spdlog.h>
#include <string_view>
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
    fmt::println(R"TEXT(Usage: foresight [options] [action]
  arguments:
        -h | --help                  Print help.

  actions:
        intercept [file]             Intercept the keyboard and print everything to stdout.
                  -g | --grab        Grab the input (stops everyone else from using the input);
                                          only use this if you know what you're doing!

        help                         Print help.
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
    auto const opts = check_opts(argc, argv);
    return run_action(opts);
} catch (std::invalid_argument const& err) {
    fmt::println(stderr, "{}", err.what());
} catch (std::exception const& err) {
    spdlog::critical("Fatal exception: {}", err.what());
} catch (...) {
    spdlog::critical("Fatal unknown exception.");
}