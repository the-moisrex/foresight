#include <stdexcept>

import fs8.mods;
import fs8.log;
import fs8.cli;
import fs8.devices.queries;

static constexpr auto args =
  fs8::arguments["Mouse"]
    .positional("mouse_device")
    .help(R"TEXT(
Usage: {{name}} [mouse_device]

Grabs the mouse and amplifies its movements by 2x.

Arguments:
    -h | --help           Print help.

Positionals:
    mouse_device          The mouse device query.
)TEXT");

int main(int const argc, char const* const* argv) try {
    using namespace fs8; // NOLINT(*-using-namespace)

    auto const parsed = args(argc, argv);
    parsed.exit_if_needed();

    static constinit auto pipeline =
      context
      | io_manager
      | intercept[mouse | required]
      | input_manager
      | scale_move[2.0f]
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
