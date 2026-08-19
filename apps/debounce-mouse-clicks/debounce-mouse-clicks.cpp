#include <charconv>
#include <chrono>
#include <cstdint>
#include <format>
#include <linux/input-event-codes.h>
#include <optional>
#include <stdexcept>
#include <string_view>

import fs8.mods;
import fs8.log;
import fs8.cli;
import fs8.devices.queries;

namespace {
    using namespace std::chrono_literals; // NOLINT(*-using-namespace)

    /// Parse a duration string like "50", "50ms", "1s", "500us" into microseconds.
    [[nodiscard]] std::optional<std::chrono::microseconds> parse_duration(std::string_view const str) noexcept {
        std::uint64_t value  = 0;
        auto const [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
        if (ec != std::errc{} || ptr == str.data()) {
            return std::nullopt;
        }

        std::string_view const suffix = str.substr(static_cast<std::size_t>(ptr - str.data()));
        if (suffix == "s") {
            return std::chrono::seconds(value);
        }
        if (suffix == "us" || suffix == "µs") {
            return std::chrono::microseconds(value);
        }
        if (suffix.empty() || suffix == "ms") {
            return std::chrono::milliseconds(value);
        }
        return std::nullopt;
    }
} // namespace

static constexpr auto args =
  fs8::arguments["Mouse"]
    .positional("mouse_device")
    .add_flag({.name        = "--time",
               .alias       = "-t",
               .help        = "Max press-to-press gap to treat as a double click (e.g. 50ms, 1s). Default: 30ms.",
               .takes_value = true})
    .help(R"TEXT(
Usage: debounce-mouse-clicks [mouse_device] [-t <time>]

Drops the occasional spurious double-clicks of a faulty mouse. A press (and its
matching release) that lands within the debounce time of the previous press of
the same button is ignored, while real clicks pass through untouched.

Arguments:
    -h | --help           Print help.
    -t | --time <time>    The debounce window, e.g. 50ms, 1s, 500us (default: 30ms).

Positionals:
    mouse_device          The mouse device query.
)TEXT");

int main(int const argc, char const* const* argv) try {
    using namespace fs8; // NOLINT(*-using-namespace)

    auto const parsed = args(argc, argv);
    if (parsed.help()) {
        parsed.print_help();
        return 0;
    }

    auto const time_threshold = [&]() {
        if (auto const t = parsed.flag_value("--time"); t.has_value()) {
            if (auto const dur = parse_duration(*t); dur.has_value()) {
                return *dur;
            }
            throw std::runtime_error(std::format("Invalid --time value: '{}' (use e.g. 50ms, 1s, 500us).", *t));
        }
        return basic_ignore_fast_double_clicks<1>::default_time_threshold;
    }();

    static constinit auto pipeline =
      context
      | io_manager
      | intercept[mouse | required | grab | matches_limit(1)]
      | input_manager
      | ignore_fast_double_clicks[BTN_LEFT, BTN_RIGHT, BTN_MIDDLE]
      | ignore_adjacent_syns
      | uinput;

    pipeline.mod(ignore_fast_double_clicks[BTN_LEFT, BTN_RIGHT, BTN_MIDDLE]).set_time_threshold(time_threshold);
    pipeline.mod(intercept).add(parsed | grab | required);
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    fs8::log("Runtime Error: {}", err.what());
    throw;
} catch (...) {
    fs8::log("Unknown Error.");
    throw;
}
