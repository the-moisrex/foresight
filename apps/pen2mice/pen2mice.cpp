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

    static constexpr auto scroll_button = mods::key_pack(BTN_MIDDLE);
    auto const            args          = std::span{argv, argv + argc};

    if (args.size() > 1) {
        constinit static auto pipeline =
          context                  // Init Context
          | intercept              // intercept the events
          | keys_status            // Save key presses
          | mice_quantifier        // Quantify the mouse movements
          | mods::ignore_big_jumps // Ignore big mouse jumps
          | on(op & pressed{BTN_MIDDLE} & pressed{BTN_LEFT} & swipe_right,
               []() {
                   std::system("qdbus6 org.kde.KWin /KWin nextDesktop");
                   return context_action::ignore_event;
               })
          | on(op & pressed{BTN_MIDDLE} & pressed{BTN_LEFT} & swipe_left,
               []() {
                   std::system("qdbus6 org.kde.KWin /KWin previousDesktop");
                   return context_action::ignore_event;
               })
          | mods::add_scroll(scroll_button, 5) // Make middle button, a scroll wheel
          | uinput;                          // put it in a virtual device

        // | mouse_history                      // Save mouse events until syn arrives
        // | mods::kalman_filter                // Smooth the mouse events

        auto files = args | drop(1) | transform([index = 0](char const* const ptr) mutable {
                         return input_file_type{.file = path{ptr}, .grab = index++ == 0};
                     });
        std::vector<input_file_type> const file_paths{files.begin(), files.end()};

        auto const out_file_template = file_paths.front().file;
        evdev      out_device{out_file_template};
        out_device.enable_event_codes(EV_REL, REL_HWHEEL, REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES);
        pipeline.set_device(out_device);
        pipeline.set_files(file_paths);
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
