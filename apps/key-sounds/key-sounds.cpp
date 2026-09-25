#include <array>
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
                            wavetable (default: basic).
    -b | --bucklespring     Shorthand for --profile bucklespring.

Positionals:
    device                  The keyboard device query (default: any keyboard).

Keyboard shortcuts:
    Double click 'Pause'    Toggle sound on/off.

Device queries are device names, paths (e.g. /dev/input/event1), or udev
terms (e.g. "name=event0", "keyboard").
)TEXT");

namespace {

    /// One row per selectable sound profile; add new profiles here.
    struct profile_entry {
        std::string_view name;
        void (*reg)();
    };

    void register_basic() noexcept {
        fs8::dynamic_synth::register_synth(fs8::basic_synth{});
    }

    void register_bucklespring() noexcept {
        fs8::dynamic_synth::register_synth(fs8::bucklespring_synth{});
    }

    void register_chime() noexcept {
        fs8::dynamic_synth::register_synth(fs8::chime_synth{});
    }

    void register_modelf() noexcept {
        fs8::dynamic_synth::register_synth(fs8::modelf_synth{});
    }

    void register_linear() noexcept {
        fs8::dynamic_synth::register_synth(fs8::linear_synth{});
    }

    void register_topre() noexcept {
        fs8::dynamic_synth::register_synth(fs8::topre_synth{});
    }

    void register_typewriter() noexcept {
        fs8::dynamic_synth::register_synth(fs8::typewriter_synth{});
    }

    void register_mx_blue() noexcept {
        fs8::dynamic_synth::register_synth(fs8::mx_blue_synth{});
    }

    void register_alps() noexcept {
        fs8::dynamic_synth::register_synth(fs8::alps_synth{});
    }

    void register_fm() noexcept {
        fs8::dynamic_synth::register_synth(fs8::fm_synth{});
    }

    void register_chiptune() noexcept {
        fs8::dynamic_synth::register_synth(fs8::chiptune_synth{});
    }

    void register_piano() noexcept {
        fs8::dynamic_synth::register_synth(fs8::piano_synth{});
    }

    void register_marimba() noexcept {
        fs8::dynamic_synth::register_synth(fs8::marimba_synth{});
    }

    void register_wavetable() noexcept {
        fs8::dynamic_synth::register_synth(fs8::wavetable_synth{});
    }

    constexpr std::array profiles = {
      profile_entry{       .name = "basic",        .reg = register_basic},
      profile_entry{.name = "bucklespring", .reg = register_bucklespring},
      profile_entry{       .name = "chime",        .reg = register_chime},
      profile_entry{      .name = "modelf",       .reg = register_modelf},
      profile_entry{      .name = "linear",       .reg = register_linear},
      profile_entry{       .name = "topre",        .reg = register_topre},
      profile_entry{  .name = "typewriter",   .reg = register_typewriter},
      profile_entry{     .name = "mx_blue",      .reg = register_mx_blue},
      profile_entry{        .name = "alps",         .reg = register_alps},
      profile_entry{          .name = "fm",           .reg = register_fm},
      profile_entry{    .name = "chiptune",     .reg = register_chiptune},
      profile_entry{       .name = "piano",        .reg = register_piano},
      profile_entry{     .name = "marimba",      .reg = register_marimba},
      profile_entry{   .name = "wavetable",    .reg = register_wavetable},
    };

    /// Look up `name` in the dispatch table and register it as the active
    /// synth.  Returns false (after listing the valid names) if unknown.
    bool select_profile(std::string_view const name) noexcept {
        for (profile_entry const& entry : profiles) {
            if (entry.name == name) {
                entry.reg();
                fs8::log("key-sounds: using {} sound profile.", name);
                return true;
            }
        }
        fs8::log("key-sounds: unknown sound profile \"{}\". Valid profiles:", name);
        for (profile_entry const& entry : profiles) {
            fs8::log("  {}", entry.name);
        }
        return false;
    }

} // namespace

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
      | on[basic_multi_click{KEY_PAUSE}, run{[](Context auto& ctx) noexcept {
               log("{} Toggle Pause triggered.", ctx.event().micro_time());
               return toggle_sound_pause(ctx);
           }}]
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
