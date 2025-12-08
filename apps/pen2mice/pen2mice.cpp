#include <chrono>
#include <linux/input-event-codes.h>
import foresight.mods;
import foresight.main.log;
import foresight.main.utils;

int main(int const argc, char const* const* argv) try {
    using namespace fs8; // NOLINT(*-using-namespace)
    using namespace std::chrono_literals;

    static constexpr auto args = arguments["pen", "usb keyboard"];
    static constinit auto pipeline =
      context
      | intercept                                  // Intercept the events
      | scheduled_emitter
      | led_status
      | keys_status                                // Save key presses
      | on(op | pressed(KEY_CAPSLOCK) | led_off(LED_CAPSL),
           context
             | abs2rel                             // Convert Drawing Tablet absolute moves into mouse moves
             | pen2mice                            // Convert the buttons
             | ignore_tablet
             | ignore_fast_left_clicks             // Ignore fast left clicks
           )
      | mice_quantifier                            // Quantify the mouse movements
      | swipe_detector                             // Detects swipes
      | on(pressed(BTN_RIGHT), ignore_start_moves) // fix right-click jumps
      | once(op & pressed(BTN_MIDDLE) & triple_click, emit(press(KEY_LEFTMETA, KEY_TAB)))
      | once(op & limit_mouse_travel(pressed(KEY_CAPSLOCK), 50) & keyup(BTN_LEFT),
             schedule_emit + press(BTN_RIGHT))
      | on(op & (op | pressed(BTN_MIDDLE) | pressed(KEY_CAPSLOCK)) & pressed(BTN_LEFT),
           context
             | on(swipe_right, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_RIGHT)))
             | on(swipe_left, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFT)))
             | on(swipe_up, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_UP)))
             | on(swipe_down, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_DOWN)))
             | ignore_mouse_moves)
      | modes(
        multi_click(KEY_CAPSLOCK),
        // Normal Mode:
        context, // empty context as the default

        // Express Mode:
        context
          | replace(KEY_D, KEY_LEFTMETA, KEY_LEFTCTRL, KEY_RIGHT)
          | replace(KEY_A, KEY_LEFTMETA, KEY_LEFTCTRL, KEY_LEFT)
          | replace(KEY_W, KEY_LEFTMETA, KEY_LEFTCTRL, KEY_UP)
          | replace(KEY_S, KEY_LEFTMETA, KEY_LEFTCTRL, KEY_DOWN)
          | replace(KEY_E, KEY_LEFTMETA, KEY_TAB)
          | on(pressed(KEY_ESC), switch_mode(0))
          | ignore_caps(caps::keyboard_alphabets))
      | ignore_big_jumps
      | ignore_adjacent_syns
      | update_mod(keys_status)
      | once(pressed(KEY_CAPSLOCK, KEY_LEFTSHIFT, KEY_ESC), exit_pipeline) // Restart/Quit
      | on(pressed(BTN_LEFT, KEY_CAPSLOCK), ignore_keys(BTN_LEFT))
      | add_scroll(op | pressed(BTN_MIDDLE) | pressed(KEY_CAPSLOCK), emit + up(BTN_MIDDLE))
      | once(longtime_released(pressed(KEY_CAPSLOCK), 200ms), emit + up(KEY_CAPSLOCK) + press(KEY_CAPSLOCK))
      | router(caps::mouse >> uinput, caps::keyboard >> uinput, caps::tablet >> uinput);

    pipeline.mod(intercept).add_devs(args(argc, argv) | find_devices | grab_inputs);
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    fs8::log("Runtime Error: {}", err.what());
} catch (...) {
    fs8::log("Unknown Error.");
}
