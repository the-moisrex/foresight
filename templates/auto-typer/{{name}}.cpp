#include <stdexcept>

import fs8;

static constexpr auto args =
  fs8::arguments["USB Keyboard"]
    .positional("keyboard_device")
    .help(R"TEXT(
Usage: {{name}} [keyboard_device]

A Foresight app created from the 'auto-typer' template: it watches your typing
and completes the pattern "@test" into the string "nice".

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
      | drop_adjacent_syns
      | uinput;

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
