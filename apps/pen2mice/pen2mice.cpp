#include <chrono>
#include <linux/input-event-codes.h>
import fs8;

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

    static constexpr auto keyboard_pipeline =
      context
      | enforce_key_state
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
                | drop_caps[caps::keyboard_alphabets]]
      | uinput;

    static constinit auto pipeline =
      context
      | pipeline_singleton
      | io_manager
      | input_manager
      | startup_key_releases
      | intercept[tablet | required | grab, keyboard | grab]
      | scheduled_emitter
      | scheduler
      | led_state
      | keys_state // Save key presses
      | mouse_history
      | on[pressed[KEY_CAPSLOCK] | led_off[LED_CAPSL],
           benchmark_all[context               // Convert Drawing Tablet absolute moves into mouse moves
                         | abs2rel
                         | pen2mice              // Convert the buttons
                         | drop_tablet
                         | drop_big_jumps
                         | drop_fast_left_clicks // Ignore fast left clicks
                         | update_mod[keys_state]
                         | update_mod[mouse_history]]]
      | on[pressed[KEY_CAPSLOCK] & keydown[KEY_B], benchmark_result[fs8::log, true]]

      | swipe_detector                           // Detects swipes
      | on[pressed[BTN_RIGHT], drop_start_moves] // fix right-click jumps
      | once[pressed[BTN_MIDDLE] & triple_click, emit[press(KEY_LEFTMETA, KEY_TAB)]]
      | once[limit_mouse_travel[pressed[KEY_CAPSLOCK], 50] & keyup[BTN_LEFT], schedule_emit + press(BTN_RIGHT)]
      | on[pressed_any[KEY_CAPSLOCK, BTN_MIDDLE] & pressed[BTN_LEFT],
           context
             | on[swipe_right, emit[press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_RIGHT)]]
             | on[swipe_left, emit[press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFT)]]
             | on[swipe_up, emit[press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_UP)]]
             | on[swipe_down, emit[press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_DOWN)]]
             | drop_mouse_moves
             | drop_mouse_clicks]
      | once[pressed[KEY_CAPSLOCK, KEY_LEFTSHIFT, KEY_ESC], exit_pipeline] // Restart/Quit
      | on[pressed[KEY_CAPSLOCK], drop_keys[BTN_LEFT]]
      | on_held[KEY_CAPSLOCK, BTN_MIDDLE, context | kalman_filter[0.3f] | mouse_to_scroll]
      | on[held[KEY_LEFTSHIFT], context | scale_move[0.5f] | scale_pen[0.5f]]
      | on[held[KEY_LEFTCTRL], low_pass_filter]
      | momentum_scroll
      | update_mod[keys_state]
      | drop_msc_scan
      | drop_zero_mouse_moves
      | drop_adjacent_syns
      | sieve
      | router[mouse >> uinput, keyboard >> keyboard_pipeline, tablet >> uinput];

    auto const parsed = args(argc, argv);
    parsed.exit_if_needed();
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
