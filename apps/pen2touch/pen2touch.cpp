#include <stdexcept>
import fs8;

static constexpr auto args =
  fs8::arguments.positional("pen_device")
    .help(R"TEXT(
Usage: pen2touch [pen_device]

Converts a drawing tablet's pen input into multitouch (touchpad) events
using the Linux Type-B multitouch protocol.

The output virtual device ("Foresight Virtual Touchpad") exposes
ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X/Y, BTN_TOUCH, and
BTN_TOOL_FINGER / BTN_TOOL_DOUBLETAP (the latter while the scroll finger
is open) so that libinput / Wayland / X.Org see a multitouch
touchpad.  The contact opens while the pen is in proximity, so
hovering already moves the cursor; BTN_TOUCH signals that contact, not
the tip.  The legacy axes (ABS_X/Y/ABS_PRESSURE) pass through;
ABS_MT_PRESSURE is intentionally not exposed, because libinput would then
require pressure-based touches that pen input never produces.

The pen tip press (BTN_TOUCH) becomes BTN_LEFT -- pressing the tip is
the left click, and advertising it turns libinput's tap-to-click off so
hover enter/leave cannot fire spurious clicks.  The third barrel button
becomes a mouse button: BTN_STYLUS3 -> side.

Holding a barrel button (BTN_STYLUS / BTN_STYLUS2) or Caps Lock
scrolls: a second contact appears next to the pen contact and libinput
reads it as a two-finger scroll (including inertial scrolling), while
the tip click is suppressed.  A quick tap of the barrel button is
still a click: BTN_STYLUS -> right, BTN_STYLUS2 -> middle.
Tilt scales the pen speed and freezes motion while the hand is
repositioning; very fast left clicks are ignored.

Arguments:
    -h | --help               Print help.
    -g | --grab               Grab the device exclusively.
    -o | --output             Output selection: stdout, uinput, evtest, live-view (default: stdout)

Positionals:
    pen_device                The drawing tablet/pen query (defaults to the tablet query when omitted).

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
      | intercept
      | keys_state
      | tilt_state[tilt_base_options{.recenter_time = 3.0F}]
      | tilt_speed[tilt_abs]
      | tilt_freeze[tilt_abs, 0.2F]
      | pen2touch
      | drop_msc_scan
      | drop_fast_left_clicks
      | drop_adjacent_syns
      | sieve
      | output;

    auto const parsed = args(argc, argv);
    parsed.exit_if_needed();
    pipeline.mod(intercept).add(parsed | fallback[tablet] | grab[parsed.has_flag("--grab")] | required);
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
