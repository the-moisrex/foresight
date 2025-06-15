#include <array>
#include <filesystem>
#include <linux/input-event-codes.h>
#include <ranges>
#include <span>
import foresight.mods;

int main(int const argc, char** argv) {
    using namespace foresight;
    using std::filesystem::path;
    using std::views::transform;

    static constexpr auto scroll_button = mods::key_pack(BTN_MIDDLE);
    auto const            args          = std::span{argv, argv + argc};

    if (args.size() > 1) {
        constinit static auto pipeline =
          context                           // Init Context
          | intercept                       // intercept the events
          | keys_status                     // Save key presses
          | mouse_history                   // Save mouse events until syn arrives
          | mice_quantifier                 // Quantify the mouse movements
          | mods::ignore_big_jumps          // Ignore big mouse jumps
          | mods::add_scroll(scroll_button) // Make middle button, a scroll wheel
          | mods::lerp                      // Smooth the mouse events
          | uinput;


        auto files = args | std::views::drop(1) | transform([index = 0](char const* const ptr) mutable {
                         return input_file_type{.file = path{ptr}, .grab = index++ == 0};
                     });
        std::vector<input_file_type> const file_paths{files.begin(), files.end()};

        auto const  out_file_template = file_paths.front().file;
        evdev const out_device{out_file_template};
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
