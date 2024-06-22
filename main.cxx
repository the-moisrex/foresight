#include <exception>
#include <format>
#include <print>
#include <string_view>
#include <utility>
import foresight.keyboard;

struct options {
    enum struct action_type : std::uint8_t {
        none = 0,
        help,
        intercept,
    } action = action_type::none; // NOLINT(*-non-private-member-variables-in-classes)

    void set_action(action_type const inp_action) {
        if (action == inp_action) {
            return;
        }
        if (action != action_type::none) {
            throw std::invalid_argument(std::format("Invalid argument syntax, two actions provided."));
        }
        action = inp_action;
    }
};

void print_help() {
    std::println(R"TEXT(Usage: foresight [options] [action]
  arguments:
        -h | --help          print help

  actions:
        intercept            intercept the keyboard and print everything to stdout
        help                 print help
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
    }

    return opts;
}

int run_action(options const opts) {
    using enum options::action_type;
    switch (opts.action) {
        case none:
        case help: {
            print_help();
            return 0;
        }
        case intercept: {
            return EXIT_SUCCESS;
        }
        default: {
            keyboard kbd;
            return kbd.loop();
        }
    }
    std::unreachable();
}

int main(int const argc, char const* const* argv) try
{
    auto const opts = check_opts(argc, argv);
    return run_action(opts);
} catch (std::exception const& err) {
    std::println(stderr, "Fatal exception: {}", err.what());
} catch (...) {
    std::println(stderr, "Fatal unknown exception.");
}
