#include "cli_args.hxx"

#include <format>
#include <stdexcept>

import fs8;

void set_action(options& opt, options::action_type const inp_action) {
    if (opt.action == inp_action) {
        return;
    }
    if (opt.action != options::action_type::none) [[unlikely]] {
        throw std::invalid_argument(std::format("Invalid argument syntax, two actions provided."));
    }
    opt.action = inp_action;
}

// NOLINTNEXTLINE(*-cognitive-complexity)
options parse_arguments(std::span<char const* const> const argv) {
    using enum options::action_type;
    using std::format;
    using std::invalid_argument;

    options opts{};
    if (argv.size() <= 1) [[unlikely]] {
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
    } else if (action_str == "capture") {
        set_action(opts, capture);
    } else if (action_str == "replay") {
        set_action(opts, replay);
    }

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
            opts.grab = true;
            continue;
        }
        if (opt == "--echo-events") {
            opts.echo_events = true;
            continue;
        }
        if (opt == "--live") {
            opts.live_view = true;
            continue;
        }
        if (opt == "--format" && index + 1 < argv.size()) {
            opts.capture_format = argv[index + 1];
            ++index;
            continue;
        }
        if (opt == "--naming" && index + 1 < argv.size()) {
            opts.capture_naming = argv[index + 1];
            ++index;
            continue;
        }
        if (opt == "--name" && index + 1 < argv.size()) {
            opts.capture_name = argv[index + 1];
            ++index;
            continue;
        }

        switch (opts.action) {
            case intercept:
            case redirect:
            case evtest:
            case live:
            case capture: {
                opts.queries.emplace_back(opt);
                break;
            }

            case replay: {
                if (opts.replay_file.empty()) {
                    opts.replay_file = opt;
                }
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
        case capture:
            if (opts.queries.empty()) {
                throw invalid_argument("Please provide a device query as an argument.");
            }
            if (opts.capture_format != "binary" && opts.capture_format != "evtest") {
                throw invalid_argument(std::format("Invalid capture format '{}'. Use 'binary' or 'evtest'.", opts.capture_format));
            }
            if (!opts.capture_name.empty() && opts.capture_naming != "manual") {
                throw invalid_argument("--name requires --naming manual.");
            }
            break;
        case replay:
            if (opts.replay_file.empty()) {
                throw invalid_argument("Please provide a capture file path as an argument.");
            }
            break;
        default: break;
    }

    return opts;
}
