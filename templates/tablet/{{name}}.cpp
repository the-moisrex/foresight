#include <stdexcept>

import fs8;

static constexpr auto args =
  fs8::arguments
    .positional("tablet_device", "keyboard_device")
    .help(R"TEXT(
Usage: {{name}} [tablet_device] [keyboard_device]

Converts a drawing tablet's absolute input into relative mouse movements.

Arguments:
    -h | --help           Print help.

Positionals:
    tablet_device         The drawing tablet device query.
    keyboard_device       Optional keyboard for mode switching.
)TEXT");

int main(int const argc, char const* const* argv) try {
    using namespace fs8; // NOLINT(*-using-namespace)

    auto const parsed = args(argc, argv);
    parsed.exit_if_needed();

    static constinit auto pipeline =
      context
      | io_manager
      | input_manager
      | intercept[tablet | required]
      | abs2rel
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
