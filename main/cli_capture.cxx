#include "cli_args.hxx"

#include <concepts>

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
                pipeline.template mod<fs8::basic_capture<FormatT, NamingT>>().set_name(opts.capture_name);
            }
        }

        signals::register_stop_signal(sig_stopper);
        signals::register_stop_signal(sig_input);
        for (auto const& q : opts.queries) {
            auto oq = fs8::owned_query{q};
            oq.grab = opts.grab;
            inpor.add(oq);
        }

        pipeline();
        return EXIT_SUCCESS;
    }
} // namespace

int run_capture_action(options const& opts) {
    auto const  is_evtest = opts.capture_format == "evtest";
    auto const& nam       = opts.capture_naming;

    if (is_evtest) {
        if (nam == "hourly") {
            return run_capture_pipeline<fs8::capture_evtest_format, fs8::capture_hourly>(opts);
        }
        if (nam == "weekly") {
            return run_capture_pipeline<fs8::capture_evtest_format, fs8::capture_weekly>(opts);
        }
        if (nam == "monthly") {
            return run_capture_pipeline<fs8::capture_evtest_format, fs8::capture_monthly>(opts);
        }
        if (nam == "uptime") {
            return run_capture_pipeline<fs8::capture_evtest_format, fs8::capture_uptime>(opts);
        }
        if (nam == "system-uptime") {
            return run_capture_pipeline<fs8::capture_evtest_format, fs8::capture_system_uptime>(opts);
        }
        if (nam == "single-file") {
            return run_capture_pipeline<fs8::capture_evtest_format, fs8::capture_single_file>(opts);
        }
        return run_capture_pipeline<fs8::capture_evtest_format, fs8::capture_daily>(opts);
    }
    if (nam == "hourly") {
        return run_capture_pipeline<fs8::capture_binary_format, fs8::capture_hourly>(opts);
    }
    if (nam == "weekly") {
        return run_capture_pipeline<fs8::capture_binary_format, fs8::capture_weekly>(opts);
    }
    if (nam == "monthly") {
        return run_capture_pipeline<fs8::capture_binary_format, fs8::capture_monthly>(opts);
    }
    if (nam == "uptime") {
        return run_capture_pipeline<fs8::capture_binary_format, fs8::capture_uptime>(opts);
    }
    if (nam == "system-uptime") {
        return run_capture_pipeline<fs8::capture_binary_format, fs8::capture_system_uptime>(opts);
    }
    if (nam == "single-file") {
        return run_capture_pipeline<fs8::capture_binary_format, fs8::capture_single_file>(opts);
    }
    return run_capture_pipeline<fs8::capture_binary_format, fs8::capture_daily>(opts);
}

int run_replay_action(options const& opts) {
    static constinit auto pipeline = fs8::context | fs8::stopper | fs8::replay | fs8::std_output;

    auto& rep = pipeline.mod(fs8::replay);
    rep.set_file(opts.replay_file);

    pipeline();
    return EXIT_SUCCESS;
}
