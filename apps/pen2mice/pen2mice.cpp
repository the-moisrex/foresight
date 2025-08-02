#include <chrono>
#include <linux/input-event-codes.h>
import foresight.mods;
import foresight.main.log;
import foresight.main.utils;

int main(int const argc, char const* const* argv) {
    using namespace foresight;
    using namespace std::chrono_literals;

    static constexpr auto args = arguments["pen", "usb keyboard"];
    static constinit auto pipeline =
      context
      | intercept                                            // Intercept the events
      | scheduled_emitter
      | led_status
      | keys_status                                          // Save key presses
      | on(op | pressed(KEY_CAPSLOCK) | led_off(LED_CAPSL),
           context                                           // Sub-context will be removed
             | abs2rel(true)                                 // Convert Pen events into Mouse events if any
             | ignore_abs                                    // Ignore absolute movements
             | ignore_fast_left_clicks                       // Ignore fast left clicks
           )
      | mice_quantifier                                      // Quantify the mouse movements
      | swipe_detector                                       // Detects swipes
      | on(pressed(BTN_RIGHT), context | ignore_start_moves) // fix right click jumps
      | on(op & pressed(BTN_MIDDLE) & triple_click, emit(press(KEY_LEFTMETA, KEY_TAB)))
      | on(op & limit_mouse_travel(pressed(KEY_CAPSLOCK), 50) & keyup(BTN_LEFT),
           schedule_emit + press(BTN_RIGHT))
      | on(op & (op | pressed(BTN_MIDDLE) | pressed(KEY_CAPSLOCK)) & pressed(BTN_LEFT),
           context
             | on(swipe_right, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_RIGHT)))
             | on(swipe_left, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFT)))
             | on(swipe_up, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_UP)))
             | on(swipe_down, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_DOWN)))
             | ignore_mouse_moves)
      | modes(pressed(KEY_LEFTCTRL, KEY_NUMLOCK),
              // Normal Mode:
              context, // empty context as the default

              // Express Mode:
              context
                | replace(KEY_KP6, press(KEY_LEFTMETA, KEY_RIGHT))
                | replace(KEY_KP4, press(KEY_LEFTMETA, KEY_LEFT))
                | replace(KEY_KP8, press(KEY_LEFTMETA, KEY_UP))
                | replace(KEY_KP2, press(KEY_LEFTMETA, KEY_DOWN))
                | ignore_caps(caps::keyboard_numpad)
                | log)
      | ignore_adjacent_syns
      | update_mod(keys_status)
      | on(pressed(BTN_LEFT, KEY_CAPSLOCK), ignore_keys(BTN_LEFT))
      | add_scroll(op | pressed(BTN_MIDDLE) | pressed(KEY_CAPSLOCK), emit + up(BTN_MIDDLE))
      | on(longtime_released(pressed(KEY_CAPSLOCK), 300ms), emit + up(KEY_CAPSLOCK) + press(KEY_CAPSLOCK))
      | router(caps::mouse >> uinput, caps::keyboard >> uinput, caps::tablet >> uinput);

    pipeline.mod(intercept).add_devs(args(argc, argv) | find_devices, grab_inputs);
    pipeline();
    return 0;
}
