#include "cli_args.hxx"

#include <algorithm>
#include <csignal>
#include <exception>
#include <print>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

import fs8;
import fs8.devices.queries;
import fs8.lib.xkb.how2type;
import fs8.systemd;

// ---- Action dispatcher ----

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
            auto& sig_input   = pipeline.mod(fs8::input_manager);
            auto& inpor       = pipeline.mod(fs8::intercept);

            signals::register_stop_signal(sig_stopper);
            signals::register_stop_signal(sig_input);
            inpor.add(opts.queries | fs8::to_queries | fs8::grab[opts.grab]);

            pipeline();
            return EXIT_SUCCESS;
        }
        case redirect: {
            if (opts.queries.size() != 1) [[unlikely]] {
                throw std::invalid_argument("Only pass one query for redirect.");
            }

            static constinit auto pipeline = fs8::context | fs8::stopper | fs8::from_input | fs8::uinput;

            auto& out         = pipeline.mod(fs8::uinput);
            auto& sig_stopper = pipeline.mod(fs8::stopper);

            auto oq              = fs8::query_from(opts.queries.front());
            oq.grab              = opts.grab;
            fs8::evdev const dev = fs8::device(oq);
            if (!dev.is_ok()) [[unlikely]] {
                throw std::runtime_error("Could not open device for the given query.");
            }
            out.set_device(dev);
            signals::register_stop_signal(sig_stopper);

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
        case capture: {
            if (opts.queries.empty()) [[unlikely]] {
                throw std::invalid_argument("Please provide a device query as an argument.");
            }
            return run_capture_action(opts);
        }
        case replay: {
            return run_replay_action(opts);
        }
        default: {
            fs8::keyboard_runner kbd;
            return kbd.loop();
        }
    }
    std::unreachable();
}

int main(int const argc, char const* const* argv) try {
    std::ignore = std::signal(SIGINT, handle_signals);
    std::ignore = std::signal(SIGTERM, handle_signals);

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
