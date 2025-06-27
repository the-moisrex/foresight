#include <filesystem>
#include <linux/input-event-codes.h>
#include <ranges>
#include <span>
import foresight.mods;

int main(int const argc, char** argv) {
    using namespace foresight;
    using std::filesystem::path;
    using std::views::drop;
    using std::views::transform;

    auto const args = std::span{argv, argv + argc};

    static constexpr auto scroll_button    = key_pack(BTN_MIDDLE);
    static constexpr auto mid_left         = op & pressed{BTN_MIDDLE} & pressed{BTN_LEFT};
    static constexpr auto ignore_mid_lefts = [](Context auto& ctx) noexcept {
        using enum context_action;
        return is_mouse_event(ctx.event()) ? ignore_event : next;
    };

    static constexpr auto main_pipeline =
      context             // Init Context
      | abs2rel           // Convert Pen events into Mouse events if any
      | ignore_abs        // Ignore absolute movements
      | keys_status       // Save key presses
      | mice_quantifier   // Quantify the mouse movements
      | swipe_detector    // Detects swipes
      | ignore_big_jumps  // Ignore big mouse jumps
      | ignore_init_moves // Fix pen small moves
      | on(op & pressed{BTN_MIDDLE} & dbl_click, emit(press(KEY_LEFTMETA, KEY_TAB))) // switch KDE activities
      | on(mid_left & swipe_right, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_RIGHT))) //
      | on(mid_left & swipe_left, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFT)))   //
      | on(mid_left & swipe_up, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_UP)))       //
      | on(mid_left & swipe_down, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_DOWN)))   //
      | on(mid_left, ignore_mid_lefts) // ignore mouse movements
      | add_scroll(scroll_button, 5);  // Make middle button, a scroll wheel

    if (args.size() > 1) {
        constinit static auto pipeline =
          context         // Init Context
          | intercept     // intercept the events
          | main_pipeline // main
          | uinput;       // put it in a virtual device


        auto files = args | drop(1) | transform([index = 0](char const* const ptr) mutable {
                         return input_file_type{.file = path{ptr}, .grab = index++ == 0};
                     });

        std::vector<input_file_type> const file_paths{files.begin(), files.end()};
        evdev                              out_device{file_paths.front().file};

        pipeline.mod(abs2rel).init(out_device);
        out_device.enable_event_codes(
          EV_REL,
          REL_WHEEL,
          REL_HWHEEL,
          REL_WHEEL_HI_RES,
          REL_HWHEEL_HI_RES,
          REL_X,
          REL_Y);
        out_device.enable_event_codes(
          EV_KEY,
          BTN_LEFT,
          BTN_RIGHT,
          BTN_MIDDLE,
          KEY_LEFT,
          KEY_RIGHT,
          KEY_UP,
          KEY_DOWN,
          KEY_LEFTMETA,
          KEY_LEFTCTRL,
          KEY_TAB);
        out_device.disable_event_type(EV_ABS);
        pipeline.mod(uinput).set_device(out_device);
        pipeline.mod(intercept).set_files(file_paths);

        pipeline();
    } else {
        (context         // Init Context
         | input         // Get the events from stdin
         | main_pipeline // main
         | output        // print the events to stdout
         )();
    }
    return 0;
}
