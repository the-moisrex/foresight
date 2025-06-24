#include <array>
#include <filesystem>
#include <linux/input-event-codes.h>
#include <print>
#include <ranges>
#include <span>
import foresight.mods;

int main(int const argc, char** argv) {
    using namespace foresight;
    using std::filesystem::path;
    using std::views::drop;
    using std::views::transform;

    static constexpr auto scroll_button    = mods::key_pack(BTN_MIDDLE);
    auto const            args             = std::span{argv, argv + argc};
    static constexpr auto mid_left         = op & pressed{BTN_MIDDLE} & pressed{BTN_LEFT};
    static constexpr auto ignore_mid_lefts = [](Context auto& ctx) noexcept {
        using enum context_action;
        return is_mouse_event(ctx.event()) ? ignore_event : next;
    };

    if (args.size() > 1) {
        constinit static auto pipeline =
          context                   // Init Context
          | intercept               // intercept the events
          | keys_status             // Save key presses
          | mice_quantifier         // Quantify the mouse movements
          | swipe_detector          // Detects swipes
          | mods::ignore_big_jumps  // Ignore big mouse jumps
          | mods::ignore_init_moves // Fix pen small moves
          | on(mid_left & swipe_right, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_RIGHT))) //
          | on(mid_left & swipe_left, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFT)))   //
          | on(mid_left & swipe_up, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_UP)))       //
          | on(mid_left & swipe_down, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_DOWN)))   //
          | on(mid_left, ignore_mid_lefts)     // ignore mouse movements
          | mods::add_scroll(scroll_button, 5) // Make middle button, a scroll wheel
          | uinput;                            // put it in a virtual device

        // | mouse_history                      // Save mouse events until syn arrives
        // | mods::kalman_filter                // Smooth the mouse events

        auto files = args | drop(1) | transform([index = 0](char const* const ptr) mutable {
                         return input_file_type{.file = path{ptr}, .grab = index++ == 0};
                     });
        std::vector<input_file_type> const file_paths{files.begin(), files.end()};

        auto const out_file_template = file_paths.front().file;
        evdev      out_device{out_file_template};
        auto&      out      = pipeline.mod(uinput);
        auto&      intcptor = pipeline.mod(intercept);
        out_device.enable_event_codes(EV_REL, REL_HWHEEL, REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES);
        out.set_device(out_device);
        intcptor.set_files(file_paths);
        pipeline();
    } else {
        (context                           // Init Context
         | input                           // Get the events from input
         | keys_status                     // Save key presses
         | mice_quantifier                 // Quantify the mouse movements
         | mods::ignore_big_jumps          // Ignore big mouse jumps
         | mods::add_scroll(scroll_button) // Make middle button, a scroll wheel
         | output                          // print the events
         )();
    }
    return 0;
}
