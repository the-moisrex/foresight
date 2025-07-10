#include <filesystem>
#include <linux/input-event-codes.h>
#include <ranges>
#include <span>
#include <print>
import foresight.mods;

int main(int const argc, char** argv) {
    using namespace foresight;
    using std::filesystem::path;
    using std::views::drop;
    using std::views::transform;

    auto const args = std::span{argv, argv + argc};

    static constexpr auto scroll_button    = key_pack(BTN_MIDDLE);
    static constexpr auto mid_left         = op & pressed{BTN_MIDDLE} & pressed{BTN_LEFT};
    static constexpr auto ignore_mid_lefts = [](Context auto& ctx) constexpr noexcept {
        using enum context_action;
        return is_mouse_event(ctx.event()) ? ignore_event : next;
    };

    static constexpr auto main_pipeline =
      context                   // Init Context
      | abs2rel                 // Convert Pen events into Mouse events if any
      | ignore_abs              // Ignore absolute movements
      | keys_status             // Save key presses
      | mice_quantifier         // Quantify the mouse movements
      | ignore_big_jumps        // Ignore big mouse jumps
      | ignore_fast_left_clicks // Ignore fast left clicks
      | ignore_init_moves       // Fix pen small moves
      | swipe_detector          // Detects swipes
      | on(pressed(BTN_RIGHT), context | ignore_big_jumps(10) | ignore_start_moves) // fix right click jumps
      | on(op & pressed{BTN_MIDDLE} & triple_click, emit(press(KEY_LEFTMETA, KEY_TAB))) //
      | on(mid_left & swipe_right, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_RIGHT)))  //
      | on(mid_left & swipe_left, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFT)))    //
      | on(mid_left & swipe_up, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_UP)))        //
      | on(mid_left & swipe_down, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_DOWN)))    //
      | on(mid_left, ignore_mid_lefts) // ignore mouse movements
      | add_scroll(scroll_button, 5);  // Make middle button, a scroll wheel

    if (args.size() > 1) {
        constinit static auto pipeline =
          context         // Init Context
          | intercept     // intercept the events
          | main_pipeline // main
          | uinput;       // put it in a virtual device


        auto vfiles = args | drop(1) | transform([index = 0](char const* const ptr) mutable {
                         return input_file_type{.file = path{ptr}, .grab = index++ == 0};
                     });
        auto files = args | drop(1) | transform([](char const* const ptr) {
                         return std::string_view{ptr};
                     });

        for (auto const dev : to_evdevs(files)) {
            std::println("Input device({}): {}", dev.physical_location(), dev.device_name());
        }

        std::vector<input_file_type> const file_paths{vfiles.begin(), vfiles.end()};
        // evdev out_device{file_paths.front().file};
        evdev out_device{(files | std::views::take(1)).front()};

        pipeline.mod(abs2rel).init(out_device);
        out_device.enable_caps(caps::pointer + caps::keyboard + caps::pointer_wheels);
        out_device.disable_event_type(EV_ABS);
        pipeline.mod(uinput).set_device(out_device);
        // pipeline.mod(intercept).add_devs(to_evdevs(files));
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
