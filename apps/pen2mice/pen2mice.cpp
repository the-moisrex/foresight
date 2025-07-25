#include <filesystem>
#include <linux/input-event-codes.h>
#include <ranges>
#include <span>
import foresight.mods;
import foresight.main.log;
import foresight.main.utils;

int main(int argc, char const* const* argv) {
    using namespace foresight;
    using namespace std::chrono_literals;
    using std::filesystem::path;
    using std::views::drop;
    using std::views::transform;

    static constexpr auto default_args = std::array{"", "pen", "usb keyboard"};
    auto const            beg          = argc == 1 ? default_args.data() : argv;
    argc                               = argc == 1 ? static_cast<int>(default_args.size()) : argc;
    auto args = std::span{beg, beg + argc} | drop(1) | transform_to<std::string_view>();

    constinit static auto pipeline =
      context
      | intercept                      // Intercept the events
      | led_status
      | keys_status                    // Save key presses
      | on(op | pressed(KEY_CAPSLOCK) | led_off(LED_CAPSL),
           context                     // Sub-context will be removed
             | abs2rel(true)           // Convert Pen events into Mouse events if any
             | ignore_abs              // Ignore absolute movements
             | ignore_big_jumps        // Ignore big mouse jumps
             | ignore_fast_left_clicks // Ignore fast left clicks
             | ignore_init_moves       // Fix pen small moves
           // | update_mod(keys_status)
           )
      | mice_quantifier // Quantify the mouse movements
      | swipe_detector  // Detects swipes
      | on(pressed(BTN_RIGHT), context | ignore_big_jumps(10) | ignore_start_moves) // fix right click jumps
      | on(op & pressed{BTN_MIDDLE} & triple_click, emit(press(KEY_LEFTMETA, KEY_TAB)))
      | on(op & (op | pressed{BTN_MIDDLE} | pressed(KEY_CAPSLOCK)) & pressed{BTN_LEFT},
           context
             | on(swipe_right, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_RIGHT)))
             | on(swipe_left, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFT)))
             | on(swipe_up, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_UP)))
             | on(swipe_down, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_DOWN)))
             | ignore_mouse_moves)
      | ignore_adjacent_syns
      | update_mod(keys_status)
      | add_scroll(op | pressed(BTN_MIDDLE) | pressed(KEY_CAPSLOCK), emit + up(BTN_MIDDLE))
      | on(longtime_released(pressed(KEY_CAPSLOCK), 300ms), emit + up(KEY_CAPSLOCK) + press(KEY_CAPSLOCK))
      | router(caps::mouse >> uinput, caps::keyboard >> uinput, caps::tablet >> uinput);

    pipeline.mod(intercept).add_devs(args | find_devices, grab_inputs);
    pipeline();
    return 0;
}
