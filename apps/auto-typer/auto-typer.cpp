#include <stdexcept>

import fs8.mods;
import fs8.log;
import fs8.cli;
import fs8.devices.queries;

static constexpr auto args = fs8::arguments["USB Keyboard"].positional("keyboard_device").help(R"TEXT(
Usage: auto-typer [keyboard_device]

Types a string when certain typed patterns are detected on the keyboard.

Arguments:
    -h | --help           Print help.

Positionals:
    keyboard_device       The USB keyboard device query.
)TEXT");

int main(int const argc, char const* const* argv) try {
    using namespace fs8; // NOLINT(*-using-namespace)

    auto const parsed = args(argc, argv);
    parsed.exit_if_needed();

    static constinit auto pipeline =
      context
      | io_manager
      | intercept[keyboard | required | matches_limit(10)]
      | input_manager
      | search_engine
      | on[typed["@test"], type_string("nice")]
      | ignore_adjacent_syns
      | to_evtest;

    pipeline.mod(intercept).add(parsed | required);
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    fs8::log("Runtime Error: {}", err.what());
    throw;
} catch (...) {
    fs8::log("Unknown Error.");
    throw;
}
