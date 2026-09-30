#include <array>
#include <charconv>
#include <cmath>
#include <linux/input-event-codes.h>
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
reads it as a two-finger scroll (including inertial scrolling), with
horizontal scrolling following the pen -- the finger mirrors the pen's
motion -- while a left click can never start.  A quick tap of the
barrel button is still a click: BTN_STYLUS -> right, BTN_STYLUS2 ->
middle, and clicking the tip while scrolling sends a middle click.
Very fast left clicks are ignored.

Fast pen motion is divided by libinput's own touchpad-acceleration
gain, so the cursor keeps the pen's speed instead of receiving
libinput's fast-motion boost; the scroll gesture itself passes
through untouched.  Pass --native-accel to hand the raw stream to
libinput instead (a non-default compositor accel-speed setting
changes the gain the compensation relies on anyway).

Caps Lock scrolling needs --keyboard: the keyboard is grabbed
exclusively and typing is re-emitted through a virtual keyboard, so a
quick Caps Lock tap still toggles caps while a hold scrolls.

Arguments:
    -h | --help               Print help.
    -g | --grab               Grab the device exclusively.
    -o | --output             Output selection: stdout, uinput, evtest, live-view (default: stdout)
    --speed <factor>          Cursor speed multiplier for the touchpad output (default: 1.0).
    --native-accel            Do not compensate libinput's fast-motion acceleration;
                              libinput accelerates the raw stream itself (default: off).
    --keyboard <query>        Open this keyboard for CapsLock-hold scrolling (e.g. "keyboard",
                              "name=event3").  Grabbed exclusively; fails if no device matches.

Positionals:
    pen_device                The drawing tablet/pen query (defaults to the tablet query when omitted).

Device queries are device names, paths (e.g. /dev/input/event1), or udev
terms (e.g. "name=event0", "tablet").
)TEXT")
    .add_flags(fs8::output_flags)
    .add_flag({.name = "--grab", .alias = "-g", .help = "grab the device exclusively"})
    .add_flag({.name = "--speed", .help = "cursor speed multiplier (default: 1.0)", .takes_value = true})
    .add_flag({.name = "--native-accel", .help = "let libinput accelerate fast motion (no compensation)"})
    .add_flag({.name = "--keyboard", .help = "keyboard query for CapsLock-hold scrolling", .takes_value = true});

int main(int const argc, char const* const* argv) try {
    using namespace fs8;

    // The touch route falls through to the top-level output (the empty
    // context returns next); the keyboard route ends in drop_event
    // because uinput returns next on a successful write and the events
    // must not reach the touchpad output behind the router.  Route caps
    // are views, so every operand must live in static storage: hoist the
    // inline cap expressions into named constexprs first.
    static constexpr auto all_keys      = caps_range<EV_KEY, 0, KEY_MAX + 1>();
    static constexpr auto distance_cap  = cap(EV_ABS, ABS_DISTANCE);
    static constexpr auto no_btn_zero   = cap(EV_KEY, BTN_0);
    static constexpr auto touch_caps    = caps::tablet + caps::mt_abs_axes + all_keys + distance_cap;
    static constexpr auto keyboard_caps = caps::keyboard - no_btn_zero;
    static constinit auto pipeline =
      context
      | singleton
      | io_manager
      | input_manager
      | startup_key_releases
      | intercept
      | keys_state
      | pen2touch
      | on_held[KEY_CAPSLOCK, context]
      | drop_msc_scan
      | drop_fast_left_clicks
      | drop_adjacent_syns
      | sieve
      | router[touch_caps >> context, keyboard_caps >> (context | enforce_key_state | uinput | drop_event)]
      | output;

    auto const parsed = args(argc, argv);
    parsed.exit_if_needed();
    pipeline.mod(intercept).add(parsed | fallback[tablet] | grab[parsed.has_flag("--grab")] | required);
    if (auto const keyboard = parsed.flag_value("--keyboard"); keyboard.has_value()) {
        pipeline.mod(intercept).add(std::array{*keyboard} | grab | required);
    }
    output_flags.configure(pipeline.mod(output), parsed);
    if (auto const speed = parsed.flag_value("--speed"); speed.has_value()) {
        float factor         = 1.0f;
        auto const [end, ec] = std::from_chars(speed->data(), speed->data() + speed->size(), factor);
        if (ec != std::errc{} || end != speed->data() + speed->size() || !std::isfinite(factor) || factor <= 0.0f) {
            fs8::log("pen2touch: invalid --speed value '{}', using 1.0", *speed);
        } else {
            pipeline.mod(pen2touch).speed(factor);
        }
    }
    // Compensate libinput's fast-motion boost by default;
    // --native-accel is the escape hatch back to the raw stream.
    pipeline.mod(pen2touch).accel_compensation(!parsed.has_flag("--native-accel"));
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    fs8::log("Runtime Error: {}", err.what());
    throw;
} catch (...) {
    fs8::log("Unknown Error.");
    throw;
}
