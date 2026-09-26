#include <stdexcept>
import fs8;

static constexpr auto args =
  fs8::arguments.positional("pen_device")
    .help(R"TEXT(
Usage: pen2touch [pen_device]

Converts a drawing tablet's pen input into multitouch (touchpad) events
using the Linux Type-B multitouch protocol.

The output virtual device exposes ABS_MT_SLOT, ABS_MT_TRACKING_ID,
ABS_MT_POSITION_X/Y, ABS_MT_PRESSURE, BTN_TOUCH, and BTN_TOOL_FINGER
so that libinput / Wayland / X.Org see a single-finger touchpad contact.

Arguments:
    -h | --help               Print help.
    -g | --grab               Grab the device exclusively.
    -o | --output             Output solution: stdout, uinput, evtest, live-view (default: stdout)

Positionals:
    pen_device                The drawing tablet/pen device query.

Device queries are device names, paths (e.g. /dev/input/event1), or udev
terms (e.g. "name=event0", "tablet").
)TEXT")
    .add_flags(fs8::output_flags)
    .add_flag({.name = "--grab", .alias = "-g", .help = "grab the device exclusively"});

int main(int const argc, char const* const* argv) try {
    using namespace fs8;

    static constinit auto pipeline =
      context
      | singleton
      | io_manager
      | input_manager
      | intercept[tablet | required]
      | keys_state
      | pen2touch
      | drop_adjacent_syns
      | sieve
      | output;

    auto const parsed = args(argc, argv);
    parsed.exit_if_needed();
    pipeline.mod(intercept).add(parsed | grab[parsed.has_flag("--grab")] | required);
    output_flags.configure(pipeline.mod(output), parsed);
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    fs8::log("Runtime Error: {}", err.what());
    throw;
} catch (...) {
    fs8::log("Unknown Error.");
    throw;
}
