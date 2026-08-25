#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <format>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

import fs8;

namespace {
    using namespace std::chrono_literals; // NOLINT(*-using-namespace)

    /// How many distinct event codes the debounce can track.
    constexpr std::size_t max_codes = 16;

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

    /// Parse an event code from a name like "BTN_LEFT" (EV_KEY implied) or
    /// "EV_ABS:ABS_X". Returns nullopt for unknown names.
    [[nodiscard]] std::optional<fs8::event_code> parse_code(std::string_view const str) noexcept {
        if (auto const colon = str.find(':'); colon != std::string_view::npos) {
            std::string const type_name{str.substr(0, colon)};
            std::string const code_name{str.substr(colon + 1)};
            int const         type = libevdev_event_type_from_name(type_name.c_str());
            if (type == -1) {
                return std::nullopt;
            }
            int const code = libevdev_event_code_from_name(static_cast<unsigned>(type), code_name.c_str());
            if (code == -1) {
                return std::nullopt;
            }
            return fs8::event_code{
              .type = static_cast<decltype(fs8::event_code::type)>(type),
              .code = static_cast<decltype(fs8::event_code::code)>(code),
            };
        }

        std::string const code_name{str};
        int const         code = libevdev_event_code_from_name(EV_KEY, code_name.c_str());
        if (code == -1) {
            return std::nullopt;
        }
        return fs8::event_code{
          .type = EV_KEY,
          .code = static_cast<decltype(fs8::event_code::code)>(code),
        };
    }

    /// Parse a comma-separated list of event codes.
    [[nodiscard]] std::optional<std::array<fs8::event_code, max_codes>> parse_codes(std::string_view str) noexcept {
        std::array<fs8::event_code, max_codes> out{};
        std::size_t                            count = 0;
        while (!str.empty()) {
            auto const                           comma = str.find(',');
            auto const                           token = str.substr(0, comma);
            std::optional<fs8::event_code> const code  = parse_code(token);
            if (!code.has_value()) {
                return std::nullopt;
            }
            if (count < out.size()) {
                out[count++] = *code;
            }
            if (comma == std::string_view::npos) {
                break;
            }
            str = str.substr(comma + 1);
        }
        return count == 0 ? std::nullopt : std::optional{out};
    }

    /// Parse the `--mode` value.
    [[nodiscard]] std::optional<fs8::debounce_mode> parse_mode(std::string_view const str) noexcept {
        if (str == "click") {
            return fs8::debounce_mode::click;
        }
        if (str == "event") {
            return fs8::debounce_mode::event;
        }
        return std::nullopt;
    }

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
    .add_flag({.name = "--time", .alias = "-t", .help = "The debounce window, e.g. 50ms, 1s, 500us (default: 30ms).", .takes_value = true})
    .add_flag({.name        = "--mode",
               .alias       = "-m",
               .help        = "'click' (default): drop a fast second press and its release; 'event': drop any event within the window.",
               .takes_value = true})
    .add_flag({.name  = "--codes",
               .alias = "-c",
               .help = "Comma-separated event codes, e.g. 'BTN_LEFT,BTN_RIGHT' or 'EV_ABS:ABS_X' (default: BTN_LEFT,BTN_RIGHT,BTN_MIDDLE).",
               .takes_value = true})
    .help(R"TEXT(
Usage: debounce [mouse_device] [options]

Drops events that arrive within the debounce window of a previous event of the
same code. By default it debounces the mouse buttons, fixing faulty mice that
occasionally double click; use --codes and --mode to debounce any other event,
such as bouncing keyboard keys, noisy tablet axes, or double-firing scroll wheels.

Arguments:
    -h | --help           Print help.
    -t | --time <time>    The debounce window, e.g. 50ms, 1s, 500us (default: 30ms).
    -m | --mode <mode>    'click' (default) or 'event'.
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

    auto const mode = [&] {
        if (auto const m = parsed.flag_value("--mode"); m.has_value()) {
            if (auto const parsed_mode = parse_mode(*m); parsed_mode.has_value()) {
                return *parsed_mode;
            }
            throw std::runtime_error(std::format("Invalid --mode value: '{}' (use 'click' or 'event').", *m));
        }
        return debounce_mode::click;
    }();

    auto const codes = [&] {
        if (auto const c = parsed.flag_value("--codes"); c.has_value()) {
            if (auto const parsed_codes = parse_codes(*c); parsed_codes.has_value()) {
                return *parsed_codes;
            }
            throw std::runtime_error(std::format("Invalid --codes value: '{}' (use e.g. 'BTN_LEFT,BTN_RIGHT' or 'EV_ABS:ABS_X').", *c));
        }
        return default_mouse_codes();
    }();

    static constinit auto pipeline =
      context
      | io_manager
      | intercept[mouse | required | grab | matches_limit(1)]
      | input_manager
      | basic_debounce<max_codes>{}
      | ignore_adjacent_syns
      | uinput;

    pipeline.mod(basic_debounce<max_codes>{}).set_codes(codes);
    pipeline.mod(basic_debounce<max_codes>{}).set_mode(mode);
    pipeline.mod(basic_debounce<max_codes>{}).set_time_threshold(time_threshold);
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
