#include "cli_args.hxx"

#include <concepts>
#include <unistd.h>

import fs8;
import fs8.devices.queries;

namespace {
    template <fs8::capture_format FormatT, fs8::capture_naming NamingT>
    int run_capture_pipeline(options const& opts) {
        static constinit auto pipeline =
          fs8::context
          | fs8::io_manager
          | fs8::idle_detector
          | fs8::intercept
          | fs8::input_manager
          | fs8::stopper
          | fs8::basic_capture<FormatT, NamingT>{FormatT{}, NamingT{}};

        auto& sig_stopper = pipeline.mod(fs8::stopper);
        auto& sig_input   = pipeline.mod(fs8::input_manager);
        auto& inpor       = pipeline.mod(fs8::intercept);

        if constexpr (std::same_as<NamingT, fs8::capture_manual>) {
            if (!opts.capture_name.empty()) {
                pipeline.mod(fs8::capture).set_name(opts.capture_name);
            }
        }

        signals::register_stop_signal(sig_stopper);
        signals::register_stop_signal(sig_input);
        inpor.add(opts.queries | fs8::to_queries | fs8::grab[opts.grab]);

        pipeline();
        return EXIT_SUCCESS;
    }

    template <fs8::capture_format FormatT, fs8::capture_naming NamingT>
    int run_piped_capture_pipeline(options const& opts) {
        static constinit auto pipeline =
          fs8::context
          | fs8::io_manager
          | fs8::idle_detector
          | fs8::from_input
          | fs8::stopper
          | fs8::basic_capture<FormatT, NamingT>{FormatT{}, NamingT{}};

        auto& sig_stopper = pipeline.mod(fs8::stopper);

        if constexpr (std::same_as<NamingT, fs8::capture_manual>) {
            if (!opts.capture_name.empty()) {
                pipeline.mod(fs8::capture).set_name(opts.capture_name);
            }
        }

        signals::register_stop_signal(sig_stopper);

        pipeline();
        return EXIT_SUCCESS;
    }

    template <fs8::capture_format FormatT, fs8::capture_naming NamingT>
    int dispatch_capture(options const& opts) {
        if (!isatty(STDIN_FILENO)) {
            return run_piped_capture_pipeline<FormatT, NamingT>(opts);
        }
        return run_capture_pipeline<FormatT, NamingT>(opts);
    }
} // namespace

int run_capture_action(options const& opts) {
    auto const  is_evtest = opts.capture_format == "evtest";
    auto const& nam       = opts.capture_naming;

    if (is_evtest) {
        if (nam == "hourly") {
            return dispatch_capture<fs8::capture_evtest_format, fs8::capture_hourly>(opts);
        }
        if (nam == "weekly") {
            return dispatch_capture<fs8::capture_evtest_format, fs8::capture_weekly>(opts);
        }
        if (nam == "monthly") {
            return dispatch_capture<fs8::capture_evtest_format, fs8::capture_monthly>(opts);
        }
        if (nam == "uptime") {
            return dispatch_capture<fs8::capture_evtest_format, fs8::capture_uptime>(opts);
        }
        if (nam == "system-uptime") {
            return dispatch_capture<fs8::capture_evtest_format, fs8::capture_system_uptime>(opts);
        }
        if (nam == "single-file") {
            return dispatch_capture<fs8::capture_evtest_format, fs8::capture_single_file>(opts);
        }
        return dispatch_capture<fs8::capture_evtest_format, fs8::capture_daily>(opts);
    }
    if (nam == "hourly") {
        return dispatch_capture<fs8::capture_binary_format, fs8::capture_hourly>(opts);
    }
    if (nam == "weekly") {
        return dispatch_capture<fs8::capture_binary_format, fs8::capture_weekly>(opts);
    }
    if (nam == "monthly") {
        return dispatch_capture<fs8::capture_binary_format, fs8::capture_monthly>(opts);
    }
    if (nam == "uptime") {
        return dispatch_capture<fs8::capture_binary_format, fs8::capture_uptime>(opts);
    }
    if (nam == "system-uptime") {
        return dispatch_capture<fs8::capture_binary_format, fs8::capture_system_uptime>(opts);
    }
    if (nam == "single-file") {
        return dispatch_capture<fs8::capture_binary_format, fs8::capture_single_file>(opts);
    }
    return dispatch_capture<fs8::capture_binary_format, fs8::capture_daily>(opts);
}

int run_replay_action(options const& opts) {
    auto const file = opts.replay_file.empty() ? std::string_view{"-"} : opts.replay_file;

    if (opts.live_view) {
        static constinit auto pipeline =
          fs8::context | fs8::stopper | fs8::replay | fs8::basic_condensed_view_output{};

        auto& rep = pipeline.mod(fs8::replay);
        rep.set_file(file);

        pipeline();
    } else {
        static constinit auto pipeline =
          fs8::context | fs8::stopper | fs8::replay | fs8::std_output;

        auto& rep = pipeline.mod(fs8::replay);
        rep.set_file(file);

        pipeline();
    }
    return EXIT_SUCCESS;
}
