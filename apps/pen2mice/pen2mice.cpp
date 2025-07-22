#include <filesystem>
#include <linux/input-event-codes.h>
#include <print>
#include <ranges>
#include <span>
import foresight.mods;
import foresight.main.log;

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

int main(int const argc, char const* const* argv) {
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
      context
      | led_status
      | on(led_off(LED_CAPSL),
           context                     // sub-context will be removed
             | abs2rel(true)           // Convert Pen events into Mouse events if any
             | ignore_abs              // Ignore absolute movements
             | ignore_big_jumps        // Ignore big mouse jumps
             | ignore_fast_left_clicks // Ignore fast left clicks
             | ignore_init_moves       // Fix pen small moves
           )
      | keys_status                    // Save key presses
      | mice_quantifier                // Quantify the mouse movements
      | swipe_detector                 // Detects swipes
      | on(pressed(BTN_RIGHT), context | ignore_big_jumps(10) | ignore_start_moves) // fix right click jumps
      | on(op & pressed{BTN_MIDDLE} & triple_click, emit(press(KEY_LEFTMETA, KEY_TAB)))
      | on(mid_left,
           on(swipe_right, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_RIGHT))),
           on(swipe_left, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFT))),
           on(swipe_up, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_UP))),
           on(swipe_down, emit(press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_DOWN))),
           ignore_mid_lefts           // ignore mouse movements
           )
      | add_scroll(scroll_button, 5); // Make 'middle button' a scroll wheel

    if (args.size() > 0) {
        static constexpr auto first_caps =
          caps::pointer + caps::keyboard + caps::pointer_wheels - caps::abs_all + caps::misc;
        static constexpr auto second_caps = caps::tablet - caps::pointer_rel_all;
        constinit static auto pipeline =
          context
          | intercept // Intercept the events
          | main_pipeline
          | router(first_caps >> uinput, second_caps >> uinput);


        pipeline.mod(intercept).add_devs(args | find_devices | only_matching | only_ok | to_evdev);

        for (evdev& dev : pipeline.mod(intercept).devices()) {
            dev.grab_input();
            // log("Input device: ({}) {}", dev.physical_location(), dev.device_name());
        }

        pipeline.mod(intercept).commit();
        pipeline.init();

        for (basic_uinput const& dev : pipeline.mod(router).uinput_devices()) {
            log("Output device: {} | {}", dev.devnode(), dev.syspath());
        }

        pipeline(no_init);
    } else {
        (context
         | input  // Get the events from stdin
         | main_pipeline
         | output // Print the events to stdout
         )();
    }
    return 0;
}
