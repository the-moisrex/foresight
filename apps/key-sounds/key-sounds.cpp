#include <linux/input-event-codes.h>
#include <stdexcept>
import fs8;

static constexpr auto args =
  fs8::arguments["Key Sounds"]
    .positional("device")
    .add_flag({.name = "--bucklespring", .alias = "-b", .help = "Use bucklespring key sounds (not yet implemented)."})
    .help(R"TEXT(
Usage: key-sounds [device] [options]

Listens to keyboard events and plays click sounds for each key press/release.
No events are grabbed or forwarded — this is a passive listener.

Arguments:
    -h | --help             Print help.
    -b | --bucklespring     Use bucklespring key sounds (not yet implemented).

Positionals:
    device                  The keyboard device query (default: any keyboard).

Keyboard shortcuts:
    Double click 'Pause'    Toggle sound on/off.

Device queries are device names, paths (e.g. /dev/input/event1), or udev
terms (e.g. "name=event0", "keyboard").
)TEXT");

int main(int const argc, char const* const* argv) try {
    using namespace fs8; // NOLINT(*-using-namespace)

    auto const parsed = args(argc, argv);
    parsed.exit_if_needed();

    if (parsed.has_flag("--bucklespring")) {
        log("key-sounds: bucklespring mode is not yet implemented.");
        return 1;
    }

    static constinit auto pipeline =
      context
      | io_manager
      | input_manager
      | intercept[keyboard | required]
      | on[basic_multi_click{KEY_PAUSE}, run{[](Context auto& ctx) noexcept {
               log("{} Toggle Pause triggered.", ctx.event().micro_time());
               return toggle_sound_pause(ctx);
           }}]
      | sound_player;

    pipeline.mod(intercept).add(parsed | required);
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    fs8::log("Runtime Error: {}", err.what());
    throw;
} catch (...) {
    fs8::log("Unknown Error.");
    throw;
}
