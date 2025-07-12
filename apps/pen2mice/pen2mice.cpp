#include <filesystem>
#include <linux/input-event-codes.h>
#include <print>
#include <ranges>
#include <span>
import foresight.mods;

namespace {
    template <typename T>
    struct construct_it_from {
        template <typename... Args>
        [[nodiscard]] constexpr T operator()(Args&&... args) noexcept(std::constructible_from<T>) {
            return T{std::forward<Args>(args)...};
        }
    };

    template <typename T>
    [[nodiscard]] constexpr auto transform_to() {
        return std::views::transform(construct_it_from<T>{});
    }

    template <typename Inp, typename T>
        requires(std::is_invocable_v<T, construct_it_from<Inp>>)
    [[nodiscard]] constexpr auto into(T&& obj) {
        return std::forward<T>(obj)(construct_it_from<Inp>{});
    }
} // namespace

int main(int const argc, char** argv) {
    using namespace foresight;
    using std::filesystem::path;
    using std::views::drop;
    using std::views::transform;

    auto args = std::span{argv, argv + argc} | drop(1) | transform_to<std::string_view>();

    static constexpr auto scroll_button    = key_pack(BTN_MIDDLE);
    static constexpr auto mid_left         = op & pressed{BTN_MIDDLE} & pressed{BTN_LEFT};
    static constexpr auto ignore_mid_lefts = [](Context auto& ctx) constexpr noexcept {
        using enum context_action;
        return is_mouse_event(ctx.event()) ? ignore_event : next;
    };

    static constexpr auto main_pipeline =
      context                   // Init Context
      | abs2rel(true)           // Convert Pen events into Mouse events if any
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

    if (args.size() > 0) {
        constinit static auto pipeline =
          context          // Init Context
          | intercept      // Intercept the events
          | main_pipeline  // Main
          | uinput_picker( // Put them into newly created virtual devices
              true,
              caps::pointer + caps::keyboard + caps::pointer_wheels - caps::abs_all, // first
              caps::tablet - caps::pointer_rel_all                                   // second virtual device
            );


        pipeline.mod(intercept).add_devs(args | to_devices() | only_matching() | only_ok() | to_evdev());

        for (evdev& dev : pipeline.mod(intercept).devices()) {
            if (dev.has_caps(caps::tablet)) {
                dev.grab_input();
            }
            std::println("Input device: ({}) {}", dev.physical_location(), dev.device_name());
        }

        pipeline.mod(intercept).commit();
        pipeline.init();

        for (auto const& dev : pipeline.mod(uinput_picker).devices()) {
            std::println("Output device: {}", dev.syspath());
        }

        pipeline(no_init);
    } else {
        (context         // Init Context
         | input         // Get the events from stdin
         | main_pipeline // main
         | output        // print the events to stdout
         )();
    }
    return 0;
}
