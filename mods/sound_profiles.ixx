// Created by moisrex on 9/25/26.

module;
#include <array>
#include <cstddef>
#include <string_view>

export module fs8.mods:sound_profiles;
import fs8.context;
import fs8.log;
import :sound;
import :bucklespring;
import :chime;
import :modelf;
import :linear;
import :topre;
import :typewriter;
import :mx_blue;
import :alps;
import :fm;
import :chiptune;
import :piano;
import :marimba;
import :wavetable;
import :sampled;

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

void register_sampled() noexcept {
    fs8::dynamic_synth::register_synth(fs8::sampled_synth{});
}

namespace {


    /// Index of the registered profile in `fs8::sound_profiles`.  Only
    /// touched from the pipeline thread (hotkey handlers run there, same
    /// as the render path), so no synchronisation is needed.
    std::size_t& current_profile_index() noexcept {
        static std::size_t index = 0;
        return index;
    }

} // namespace

export namespace fs8 {

    /// One row per selectable sound profile; add new profiles here.
    struct sound_profile_entry {
        std::string_view name;
        void (*reg)() noexcept;
    };

    /// The selectable profiles, in `--profile` / `Meta+N` order.  Position
    /// in this table is the profile's 1-based number shown in help texts.
    inline constexpr std::array sound_profiles = {
      sound_profile_entry{       .name = "basic",        .reg = register_basic},
      sound_profile_entry{.name = "bucklespring", .reg = register_bucklespring},
      sound_profile_entry{       .name = "chime",        .reg = register_chime},
      sound_profile_entry{      .name = "modelf",       .reg = register_modelf},
      sound_profile_entry{      .name = "linear",       .reg = register_linear},
      sound_profile_entry{       .name = "topre",        .reg = register_topre},
      sound_profile_entry{  .name = "typewriter",   .reg = register_typewriter},
      sound_profile_entry{     .name = "mx_blue",      .reg = register_mx_blue},
      sound_profile_entry{        .name = "alps",         .reg = register_alps},
      sound_profile_entry{          .name = "fm",           .reg = register_fm},
      sound_profile_entry{    .name = "chiptune",     .reg = register_chiptune},
      sound_profile_entry{       .name = "piano",        .reg = register_piano},
      sound_profile_entry{     .name = "marimba",      .reg = register_marimba},
      sound_profile_entry{   .name = "wavetable",    .reg = register_wavetable},
      sound_profile_entry{     .name = "sampled",      .reg = register_sampled},
    };

    /// Number of selectable profiles.
    [[nodiscard]] std::size_t sound_profile_count() noexcept {
        return sound_profiles.size();
    }

    /// 0-based index of the registered profile in `sound_profiles`.
    [[nodiscard]] std::size_t current_sound_profile_index() noexcept {
        return current_profile_index();
    }

    /// Name of the registered profile.
    [[nodiscard]] std::string_view current_sound_profile_name() noexcept {
        return sound_profiles[current_profile_index()].name;
    }

    /// Register the profile at `index` as the active synth.  Returns false
    /// (logging the valid range) when the index is out of bounds.
    bool select_sound_profile_at(std::size_t const index) noexcept {
        if (index >= sound_profiles.size()) {
            log("sound: profile #{} does not exist — there are only {} profiles.", index + 1, sound_profiles.size());
            return false;
        }
        current_profile_index() = index;
        sound_profiles[index].reg();
        log("sound: using {} sound profile.", sound_profiles[index].name);
        return true;
    }

    /// Look up `name` in the dispatch table and register it as the active
    /// synth.  Returns false (after listing the valid names) if unknown.
    bool select_profile(std::string_view const name) noexcept {
        for (std::size_t i = 0; i < sound_profiles.size(); ++i) {
            if (sound_profiles[i].name == name) {
                return select_sound_profile_at(i);
            }
        }
        log("sound: unknown sound profile \"{}\". Valid profiles:", name);
        for (sound_profile_entry const& entry : sound_profiles) {
            log("  {}", entry.name);
        }
        return false;
    }

    /// Step the active profile by `delta`, wrapping around the table.
    bool cycle_sound_profile(int const delta) noexcept {
        auto const count = static_cast<int>(sound_profiles.size());
        auto const cur   = static_cast<int>(current_profile_index());
        auto const next  = ((cur + delta) % count + count) % count; // wrap both ways
        return select_sound_profile_at(static_cast<std::size_t>(next));
    }

    /// Switch to the next profile (wraps).  Returns `next`, so the trigger
    /// key still clicks — with the newly selected profile.
    constexpr struct [[nodiscard]] basic_next_sound_profile {
        template <Context CtxT>
        context_action operator()(CtxT&) const noexcept {
            (void) cycle_sound_profile(+1);
            return context_action::next;
        }
    } next_sound_profile;

    /// Switch to the previous profile (wraps).  Returns `next`, so the
    /// trigger key still clicks — with the newly selected profile.
    constexpr struct [[nodiscard]] basic_prev_sound_profile {
        template <Context CtxT>
        context_action operator()(CtxT&) const noexcept {
            (void) cycle_sound_profile(-1);
            return context_action::next;
        }
    } prev_sound_profile;

    /// Select a profile by its 0-based table index (`Meta+1` → `[0]`).
    /// Returns `next`, so the trigger key still clicks — with the newly
    /// selected profile.
    constexpr struct [[nodiscard]] basic_select_sound_profile {
      private:
        std::size_t index = 0;

      public:
        constexpr basic_select_sound_profile() noexcept = default;

        explicit constexpr basic_select_sound_profile(std::size_t const in_index) noexcept : index{in_index} {}

        consteval basic_select_sound_profile operator[](std::size_t const in_index) const noexcept {
            return basic_select_sound_profile{in_index};
        }

        template <Context CtxT>
        context_action operator()(CtxT&) const noexcept {
            (void) select_sound_profile_at(index);
            return context_action::next;
        }
    } select_sound_profile;

} // namespace fs8
