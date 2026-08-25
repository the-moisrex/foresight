#include <stdexcept>

import fs8.mods;
import fs8.log;
import fs8.cli;
import fs8.devices.queries;

static constexpr auto args =
  fs8::arguments["USB Keyboard"]
    .positional("keyboard_device")
    .help(R"TEXT(
Usage: {{name}} [keyboard_device]

Grabs the keyboard and swaps CapsLock with Escape.

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
      | intercept[keyboard | required]
      | input_manager
      | keys_status
      | on[pressed[KEY_CAPSLOCK], replace[KEY_CAPSLOCK, KEY_ESC]]
      | update_mod[keys_status]
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
