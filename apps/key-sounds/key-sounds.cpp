#include <linux/input-event-codes.h>
#include <stdexcept>
#include <string_view>
import fs8;

static constexpr auto args =
  fs8::arguments["Key Sounds"]
    .positional("device")
    .add_flag({.name = "--profile", .alias = "-p", .help = "Sound profile to use (default: basic).", .takes_value = true})
    .add_flag({.name = "--bucklespring", .alias = "-b", .help = "Shorthand for --profile bucklespring."})
    .help(R"TEXT(
Usage: key-sounds [device] [options]

Listens to keyboard events and plays click sounds for each key press/release.
No events are grabbed or forwarded — this is a passive listener.

Arguments:
    -h | --help             Print help.
    -p | --profile <name>   Sound profile: basic, bucklespring, chime, modelf, linear, topre,
                            typewriter, mx_blue, alps, fm, chiptune, piano, marimba,
                            wavetable, sampled (default: basic).
    -b | --bucklespring     Shorthand for --profile bucklespring.

Positionals:
    device                  The keyboard device query (default: any keyboard).

Keyboard shortcuts:
    Double click 'Pause'    Toggle sound on/off.
    Meta + F9 / F10         Next / previous sound profile.
    Meta + 1..9             Select profile #1..#9 of the list above
                            (#1 basic, #2 bucklespring, ... #9 alps).
    Meta + VolumeDown/Up    Lower / raise click volume (-3 dB / +3 dB).

Device queries are device names, paths (e.g. /dev/input/event1), or udev
terms (e.g. "name=event0", "keyboard").
)TEXT");

int main(int const argc, char const* const* argv) try {
    using namespace fs8; // NOLINT(*-using-namespace)

    auto const parsed = args(argc, argv);
    parsed.exit_if_needed();

    auto setup = [&](auto& pipe) {
        pipe.mod(intercept).add(parsed | required);
    };

    std::string_view profile = "basic";
    if (auto const selected = parsed.flag_value("--profile")) {
        profile = *selected;
    } else if (parsed.has_flag("--bucklespring")) {
        profile = "bucklespring";
    }
    if (!select_profile(profile)) {
        return 1;
    }

    static constinit auto pipeline =
      context
      | io_manager
      | input_manager
      | intercept[keyboard | required]
      | keys_state
      | on[basic_multi_click{KEY_PAUSE}, run{[](Context auto& ctx) noexcept {
               log("{} Toggle Pause triggered.", ctx.event().micro_time());
               return toggle_sound_pause(ctx);
           }}]
      // Profile switching: the handler runs before the player, so the
      // trigger key clicks with the *new* profile already active.
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_F9], next_sound_profile]
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_F10], prev_sound_profile]
      // Meta+1..9 → profiles #1..#9 (see `--help` for the numbered list).
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_1], select_sound_profile[0]]
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_2], select_sound_profile[1]]
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_3], select_sound_profile[2]]
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_4], select_sound_profile[3]]
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_5], select_sound_profile[4]]
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_6], select_sound_profile[5]]
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_7], select_sound_profile[6]]
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_8], select_sound_profile[7]]
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_9], select_sound_profile[8]]
      // Master click volume: applied by the mixer, so it also affects
      // sounds that are already playing.
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_VOLUMEDOWN], sound_volume_down]
      | on[pressed_any[KEY_LEFTMETA, KEY_RIGHTMETA] & keydown[KEY_VOLUMEUP], sound_volume_up]
      | basic_sound_player(dynamic_synth{});
    setup(pipeline);
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    fs8::log("Runtime Error: {}", err.what());
    throw;
} catch (...) {
    fs8::log("Unknown Error.");
    throw;
}
