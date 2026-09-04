#include <array>
#include <cstdint>
#include <format>
#include <linux/input-event-codes.h>
#include <stdexcept>
#include <string_view>

import fs8;
import fs8.parsing;

namespace {
    /// How many distinct event codes the debounce can track.
    constexpr std::size_t max_codes = 16;

    /// The default mouse-button codes when `--codes` is not given.
    [[nodiscard]] constexpr std::array<fs8::event_code, max_codes> default_mouse_codes() noexcept {
        std::array<fs8::event_code, max_codes> out{};
        out[0] = fs8::event_code{.type = EV_KEY, .code = BTN_LEFT};
        out[1] = fs8::event_code{.type = EV_KEY, .code = BTN_RIGHT};
        out[2] = fs8::event_code{.type = EV_KEY, .code = BTN_MIDDLE};
        return out;
    }
} // namespace

static constexpr auto args =
  fs8::arguments["Mouse"]
    .positional("mouse_device")
    .add_flags(fs8::output_flags)
    .add_flag({.name = "--time", .alias = "-t", .help = "The debounce window, e.g. 50ms, 1s, 500us (default: 30ms).", .takes_value = true})
    .add_flag({.name  = "--codes",
               .alias = "-c",
               .help = "Comma-separated event codes, e.g. 'BTN_LEFT,BTN_RIGHT' or 'EV_ABS:ABS_X' (default: BTN_LEFT,BTN_RIGHT,BTN_MIDDLE).",
               .takes_value = true})
    .help(R"TEXT(
Usage: debounce [mouse_device] [options]

Drops events that arrive within the debounce window of a previous event of the
same code. By default it debounces the mouse buttons, fixing faulty mice that
occasionally double click; use --codes to debounce any other event, such as
bouncing keyboard keys, noisy tablet axes, or double-firing scroll wheels.

Arguments:
    -h | --help           Print help.
    -o | --output <type>  Output: stdout, uinput, evtest, live-view (default: stdout).
    -t | --time <time>    The debounce window, e.g. 50ms, 1s, 500us (default: 30ms).
    -m | --no-grab        Do not grab the input device.
    -c | --codes <codes>  Comma-separated event codes, e.g. 'BTN_LEFT,BTN_RIGHT'
                          or 'EV_ABS:ABS_X' (default: BTN_LEFT,BTN_RIGHT,BTN_MIDDLE).

Positionals:
    mouse_device          The mouse/input device query.
)TEXT");

int main(int const argc, char const* const* argv) try {
    using namespace fs8; // NOLINT(*-using-namespace)

    auto const parsed = args(argc, argv);
    parsed.exit_if_needed();

    auto const time_threshold = [&] {
        if (auto const t = parsed.flag_value("--time"); t.has_value()) {
            if (auto const dur = parse_duration(*t); dur.has_value()) {
                return *dur;
            }
            throw std::runtime_error(std::format("Invalid --time value: '{}' (use e.g. 50ms, 1s, 500us).", *t));
        }
        return basic_debounce<1>::default_time_threshold;
    }();

    auto const codes = [&] {
        if (auto const c = parsed.flag_value("--codes"); c.has_value()) {
            if (auto const parsed_codes = parse_codes<max_codes>(*c); parsed_codes.has_value()) {
                return *parsed_codes;
            }
            throw std::runtime_error(std::format("Invalid --codes value: '{}' (use e.g. 'BTN_LEFT,BTN_RIGHT' or 'EV_ABS:ABS_X').", *c));
        }
        return default_mouse_codes();
    }();

    static constinit auto pipeline =
      context
      | io_manager
      | intercept[mouse | required | matches_limit[1]]
      | input_manager
      | basic_debounce<max_codes>{}
      | drop_adjacent_syns
      | output;

    output_flags.configure(pipeline.mod(output), parsed);
    pipeline.mod(basic_debounce<max_codes>{}).set_codes(codes);
    pipeline.mod(basic_debounce<max_codes>{}).set_time_threshold(time_threshold);
    pipeline.mod(intercept).add(parsed | grab[!parsed.has_flag("--no-grab")] | required);
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    fs8::log("Runtime Error: {}", err.what());
    throw;
} catch (...) {
    fs8::log("Unknown Error.");
    throw;
}
