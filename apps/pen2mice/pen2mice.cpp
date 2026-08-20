#include <chrono>
#include <linux/input-event-codes.h>
import fs8.mods;
import fs8.log;
import fs8.cli;
import fs8.devices.queries;

static constexpr auto args = fs8::arguments.positional("pen_device", "usb_keyboard_device").help(R"TEXT(
Usage: pen2mice [pen_device] [usb_keyboard_device]

Converts a drawing tablet's absolute input into mouse movements and clicks
with extended features like gestures, express mode, and scroll enhancement.

Arguments:
    -h | --help               Print help.

Positionals:
    pen_device                The drawing tablet/pen device query.
    usb_keyboard_device       The USB keyboard device query.

Device queries are device names, paths (e.g. /dev/input/event1), or udev
terms (e.g. "name=event0", "keyboard").
)TEXT");

int main(int const argc, char const* const* argv) try {
    using namespace fs8; // NOLINT(*-using-namespace)
    using namespace std::chrono_literals;

    auto const parsed = args(argc, argv);
    if (parsed.help()) {
        parsed.print_help();
        return 0;
    }

    static constinit auto pipeline =
      context
      | io_manager
      | input_manager
      | intercept[tablet | required | grab, keyboard | required | grab]
      | scheduled_emitter
      | led_status
      | keys_status                                // Save key presses
      | on[op | pressed[KEY_CAPSLOCK] | led_off[LED_CAPSL],
           context
             | abs2rel                             // Convert Drawing Tablet absolute moves into mouse moves
             | pen2mice                            // Convert the buttons
             | ignore_tablet
             | ignore_big_jumps
             | ignore_fast_left_clicks]            // Ignore fast left clicks
      | mice_quantifier                            // Quantify the mouse movements
      | swipe_detector                             // Detects swipes
      | on[pressed[BTN_RIGHT], ignore_start_moves] // fix right-click jumps
      | once[op & pressed[BTN_MIDDLE] & triple_click, emit[press(KEY_LEFTMETA, KEY_TAB)]]
      | once[op & limit_mouse_travel[pressed[KEY_CAPSLOCK], 50] & keyup(BTN_LEFT), schedule_emit + press(BTN_RIGHT)]
      | on[op & (op | pressed[BTN_MIDDLE] | pressed[KEY_CAPSLOCK]) & pressed[BTN_LEFT],
           context
             | on[swipe_right, emit[press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_RIGHT)]]
             | on[swipe_left, emit[press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFT)]]
             | on[swipe_up, emit[press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_UP)]]
             | on[swipe_down, emit[press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_DOWN)]]
             | ignore_mouse_moves]
      | modes[multi_click[KEY_RIGHTCTRL],
              // Normal Mode:
              context, // empty context as the default

              // Express Mode:
              context
                | replace[KEY_D, KEY_LEFTMETA, KEY_LEFTCTRL, KEY_RIGHT]
                | replace[KEY_A, KEY_LEFTMETA, KEY_LEFTCTRL, KEY_LEFT]
                | replace[KEY_W, KEY_LEFTMETA, KEY_LEFTCTRL, KEY_UP]
                | replace[KEY_S, KEY_LEFTMETA, KEY_LEFTCTRL, KEY_DOWN]
                | replace[KEY_E, KEY_LEFTMETA, KEY_TAB]
                | on[pressed[KEY_ESC], switch_mode[0]]
                | ignore_caps[caps::keyboard_alphabets]]
      | ignore_adjacent_syns
      | update_mod[keys_status]
      | once[pressed[KEY_CAPSLOCK, KEY_LEFTSHIFT, KEY_ESC], exit_pipeline] // Restart/Quit
      | on[pressed[BTN_LEFT, KEY_CAPSLOCK], ignore_keys[BTN_LEFT]]
      | on_held[KEY_CAPSLOCK, BTN_MIDDLE, mouse_to_scroll]
      | router[caps::mouse >> uinput, caps::keyboard >> uinput, caps::tablet >> uinput];

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
