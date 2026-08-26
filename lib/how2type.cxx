// Created by moisrex on 10/13/25.

module;
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <linux/input.h>
#include <memory>
#include <print>
#include <ranges>
#include <string>
#include <vector>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>
module fs8.lib.xkb.how2type;
import fs8.log;
import fs8.event;
import fs8.lib.mod_parser;

using fs8::xkb::key_position;

constexpr std::size_t MAX_TYPE_MAP_ENTRIES = 32;

namespace {
    struct modifier_map_entry {
        xkb_mod_index_t index;
        std::uint16_t   keycode;
        xkb_mod_mask_t  mask;
    };

    /**
     * Thread-safe singleton accessor for a fixed modifier map.
     *
     * Lazily builds the map once for the given keymap. No dynamic allocation,
     * no global state mutation, and safe across threads (per C++11 guarantees).
     */
    std::array<modifier_map_entry, 6U> const &get_modmap(xkb_keymap *keymap) {
        static std::array<modifier_map_entry, 6U> map{};
        static bool                               initialized = false;

        if (!initialized) {
            map[0].index   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
            map[0].keycode = KEY_LEFTSHIFT;
            map[0].mask    = map[0].index != XKB_MOD_INVALID ? xkb_keymap_mod_get_mask2(keymap, map[0].index) : 0;

            map[1].index   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
            map[1].keycode = KEY_LEFTCTRL;
            map[1].mask    = map[1].index != XKB_MOD_INVALID ? xkb_keymap_mod_get_mask2(keymap, map[1].index) : 0;

            map[2].index   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
            map[2].keycode = KEY_LEFTALT;
            map[2].mask    = map[2].index != XKB_MOD_INVALID ? xkb_keymap_mod_get_mask2(keymap, map[2].index) : 0;

            map[3].index   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO);
            map[3].keycode = KEY_LEFTMETA;
            map[3].mask    = map[3].index != XKB_MOD_INVALID ? xkb_keymap_mod_get_mask2(keymap, map[3].index) : 0;

            map[4].index   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
            map[4].keycode = KEY_CAPSLOCK;
            map[4].mask    = map[4].index != XKB_MOD_INVALID ? xkb_keymap_mod_get_mask2(keymap, map[4].index) : 0;

            // Mod5 = ISO_Level3_Shift, typically the right Alt (AltGr) key
            map[5].index   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD5);
            map[5].keycode = KEY_RIGHTALT;
            map[5].mask    = map[5].index != XKB_MOD_INVALID ? xkb_keymap_mod_get_mask2(keymap, map[5].index) : 0;

            initialized = true;
        }

        return map;
    }

    /**
     * Invoke a callable for each modifier event based on a xkb_mod_mask_t.
     *
     * - modmap: automatically fetched from get_modmap().
     * - mask:   active modifier mask.
     * - pressed: true for key press, false for key release.
     * - emit:   callable taking (const struct user_event&).
     */
    template <typename EmitFunc>
    bool invoke_mod_events(xkb_keymap *keymap, xkb_mod_mask_t const mask, bool const pressed, EmitFunc &&emit) {
        auto const &modmap = get_modmap(keymap);

        bool            mod_found = false;
        fs8::user_event ev{};
        // struct timeval     now{};
        // gettimeofday(&now, nullptr);

        for (auto const &m : modmap) {
            if (!m.mask) {
                continue;
            }

            if ((mask & m.mask) != m.mask) {
                continue;
            }

            // ev.time  = now;
            ev.type  = EV_KEY;
            ev.code  = m.keycode;
            ev.value = pressed ? 1 : 0;
            emit(ev);
            mod_found = true;
        }
        return mod_found;
    }

    /// For each keycode/layout/level with single keysym equal to keysym, call the callback with the
    /// key_position (one per mask returned).
    bool on_keypos(fs8::xkb::keymap const &map, xkb_keysym_t const keysym, fs8::xkb::handle_keysym_callback callback) {
        xkb_keycode_t const       min_keycode = xkb_keymap_min_keycode(map.get());
        xkb_keycode_t const       max_keycode = xkb_keymap_max_keycode(map.get());
        std::vector<xkb_keysym_t> seen_syms;
        bool                      found_positions = false;

        // We'll keep track of (keycode, layout) we already added for deduping of lower levels
        for (xkb_keycode_t keycode = min_keycode; keycode <= max_keycode; ++keycode) {
            char const *keyname = xkb_keymap_key_get_name(map.get(), keycode);
            if (keyname == nullptr) {
                continue;
            }

            xkb_layout_index_t const num_layouts = xkb_keymap_num_layouts_for_key(map.get(), keycode);
            for (xkb_layout_index_t layout = 0; layout < num_layouts; ++layout) {
                xkb_level_index_t const num_levels = xkb_keymap_num_levels_for_key(map.get(), keycode, layout);

                // keep track of which keysyms we have seen on lower levels of this key/layout
                seen_syms.clear();
                seen_syms.reserve(num_levels);

                for (xkb_level_index_t level = 0; level < num_levels; ++level) {
                    xkb_keysym_t const *syms  = nullptr;
                    int const           nsyms = xkb_keymap_key_get_syms_by_level(map.get(), keycode, layout, level, &syms);

                    if (nsyms != 1) {
                        continue;              // only care about single keysym per level
                    }

                    auto const sym0 = syms[0]; // NOLINT(*-pro-bounds-pointer-arithmetic)
                    if (sym0 != keysym) {
                        // If this keysym has already been seen in a lower level for the same key/layout,
                        // skip to avoid combinatorial explosion (original logic kept only lowest).
                        bool found = false;
                        for (auto const symbol : seen_syms) {
                            if (symbol == sym0) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            seen_syms.push_back(sym0);
                            // Possibly this sym0 contributes to compose sequences; we don't add that here.
                        }
                        continue;
                    }

                    // Found keysym at this keycode/layout/level, collect masks
                    std::array<xkb_mod_mask_t, MAX_TYPE_MAP_ENTRIES> masks{};
                    auto const n_masks = xkb_keymap_key_get_mods_for_level(map.get(), keycode, layout, level, masks.data(), masks.size());

                    // If there are no masks reported, still push a default mask 0
                    if (n_masks == 0) {
                        callback({.keycode = keycode, .layout = layout, .level = level, .mask = 0});
                        found_positions = true;
                        // entry->positions.emplace_back();
                    } else {
                        found_positions |= n_masks > 0;
                        for (std::size_t mi = 0; mi < n_masks; ++mi) {
                            callback({.keycode = keycode, .layout = layout, .level = level, .mask = masks.at(mi)});
                            // entry->positions.emplace_back();
                        }
                    }
                }
            }
        }
        return found_positions;
    }
} // namespace

void fs8::xkb::how2type::on_keysym(keymap const &map, xkb_keysym_t const target_keysym, handle_keysym_callback callback) noexcept {
    bool done = false;
    on_keypos(map, target_keysym, [&](key_position const &position) {
        // Return the first direct position (first-found strategy)
        if (done) {
            return;
        }
        callback(position);
        done = true;
    });
}

namespace {
    /// Naive xkb->evdev mapping using the historical +8 X11 offset
    [[nodiscard]] constexpr int keycode_to_evdev(xkb_keycode_t const xkb_k) noexcept {
        // Historically for X11-compatible maps: xkb_keycode == evdev + 8
        if (xkb_k <= 8) {
            return -1;
        }
        return static_cast<int>(xkb_k) - 8;
    }

    /// Emit a press/release (with SYN_REPORTs and any required modifiers) for a physical key position.
    void emit_key_at(fs8::xkb::keymap const &map, fs8::xkb::key_position const &pos, fs8::user_event_callback callback) {
        int const evcode = keycode_to_evdev(pos.keycode);
        if (evcode < 0) {
            // can't map this key; abort
            fs8::log("Warning: can't map xkb keycode {} to evdev code", static_cast<unsigned>(pos.keycode));
            return;
        }

        bool const requires_mods = pos.mask != 0;

        // Prepare a press event
        fs8::user_event const ev_press{
          .type  = EV_KEY,
          .code  = static_cast<std::uint16_t>(evcode),
          .value = 1 // press
        };
        // time can be zeroed (caller can set real timestamps if desired)

        // Prepare a release event
        fs8::user_event const ev_release{
          .type  = EV_KEY,
          .code  = static_cast<std::uint16_t>(evcode),
          .value = 0 // release
        };

        // SYN_REPORT
        constexpr fs8::user_event ev_syn{.type = EV_SYN, .code = SYN_REPORT, .value = 0};

        std::array<xkb_mod_mask_t, MAX_TYPE_MAP_ENTRIES> masks{};
        std::size_t                                      num_masks = 0;

        if (requires_mods) {
            // todo: can we cache these results if this function is heavy?
            num_masks = xkb_keymap_key_get_mods_for_level(map.get(), pos.keycode, pos.layout, pos.level, masks.data(), masks.size());

            if (num_masks
                > 0
                && invoke_mod_events(map.get(),
                                     masks.at(0),
                                     true,
                                     [&](fs8::user_event const &event) {
                                         callback(event);
                                     }))
            {
                callback(ev_syn);
            }
        }

        callback(ev_press);
        callback(ev_syn);
        callback(ev_release);
        callback(ev_syn);

        // release the evs
        if (requires_mods
            && num_masks
            > 0
            && invoke_mod_events(map.get(),
                                 masks.at(0),
                                 false,
                                 [&](fs8::user_event const &event) {
                                     callback(event);
                                 }))
        {
            callback(ev_syn);
        }
    }

    /// Emit a single key that produces `keysym` on this keymap (returns false if not typable).
    bool emit_keysym(fs8::xkb::keymap const &map, xkb_keysym_t const keysym, fs8::user_event_callback callback) {
        bool emitted = false;
        fs8::xkb::how2type::on_keysym(map, keysym, [&](fs8::xkb::key_position const &pos) {
            if (emitted) {
                return;
            }
            emitted = true;
            emit_key_at(map, pos, callback);
        });
        return emitted;
    }

    /// Compose table for the current locale (function-local static: built once, thread-safe per C++11).
    xkb_compose_table *compose_table() noexcept {
        static xkb_compose_table *const table = [] {
            // libxkbcommon's NULL-locale path (which relies on the process locale) can misbehave,
            // so resolve the locale ourselves and fall back to the default English compose table.
            auto locale_for_compose = []() -> std::string {
                for (char const *name : {"LC_ALL", "LC_CTYPE", "LANG"}) {
                    if (char const *value = std::getenv(name); value != nullptr && *value != '\0') {
                        std::string const locale{value};
                        // C/POSIX locales have no dedicated compose file; use the default English table
                        if (locale != "C" && locale != "POSIX" && locale != "C.UTF-8" && locale != "POSIX.UTF-8") {
                            return locale;
                        }
                        break;
                    }
                }
                return "en_US.UTF-8";
            };

            xkb_context *const ctx    = fs8::xkb::get_default_context().get();
            xkb_compose_table *result = xkb_compose_table_new_from_locale(ctx, locale_for_compose().c_str(), XKB_COMPOSE_COMPILE_NO_FLAGS);
            if (result == nullptr) [[unlikely]] {
                result = xkb_compose_table_new_from_locale(ctx, "en_US.UTF-8", XKB_COMPOSE_COMPILE_NO_FLAGS);
            }
            if (result == nullptr) [[unlikely]] {
                fs8::log("Warning: failed to load a compose table; composed characters can't be typed");
            }
            return result;
        }();
        return table;
    }

    /// All distinct keysyms physically present on this keymap (any key/layout/level).
    std::vector<xkb_keysym_t> collect_typable_keysyms(xkb_keymap *keymap) {
        std::vector<xkb_keysym_t> syms;
        for (xkb_keycode_t keycode = xkb_keymap_min_keycode(keymap); keycode <= xkb_keymap_max_keycode(keymap); ++keycode) {
            xkb_layout_index_t const num_layouts = xkb_keymap_num_layouts_for_key(keymap, keycode);
            for (xkb_layout_index_t layout = 0; layout < num_layouts; ++layout) {
                xkb_level_index_t const num_levels = xkb_keymap_num_levels_for_key(keymap, keycode, layout);
                for (xkb_level_index_t level = 0; level < num_levels; ++level) {
                    xkb_keysym_t const *keysyms  = nullptr;
                    int const           num_syms = xkb_keymap_key_get_syms_by_level(keymap, keycode, layout, level, &keysyms);
                    for (int i = 0; i < num_syms; ++i) {
                        if (keysyms[i] != XKB_KEY_NoSymbol) {
                            syms.push_back(keysyms[i]);
                        }
                    }
                }
            }
        }
        std::sort(syms.begin(), syms.end());
        syms.erase(std::unique(syms.begin(), syms.end()), syms.end());
        return syms;
    }

    constexpr int MAX_COMPOSE_DEPTH = 3;

    /// Search the compose table for a sequence producing `target`, using only keysyms that are
    /// physically typable on this keymap. Fills `path` with the sequence when found.
    bool find_composed(
      xkb_compose_state               *state,
      xkb_keysym_t const               target,
      std::vector<xkb_keysym_t> const &candidates,
      std::vector<xkb_keysym_t>       &path,
      int const                        depth,
      int                             &feed_budget) {
        for (xkb_keysym_t const candidate : candidates) {
            if (--feed_budget < 0) [[unlikely]] {
                return false;
            }

            xkb_compose_state_reset(state);
            for (xkb_keysym_t const key : path) {
                xkb_compose_state_feed(state, key);
            }
            if (xkb_compose_state_feed(state, candidate) != XKB_COMPOSE_FEED_ACCEPTED) {
                continue;
            }

            auto const status = xkb_compose_state_get_status(state);
            if (status == XKB_COMPOSE_COMPOSED) {
                if (xkb_compose_state_get_one_sym(state) == target) {
                    path.push_back(candidate);
                    return true;
                }
                continue;
            }
            if (status != XKB_COMPOSE_COMPOSING || depth == MAX_COMPOSE_DEPTH) {
                continue;
            }

            path.push_back(candidate);
            if (find_composed(state, target, candidates, path, depth + 1, feed_budget)) {
                return true;
            }
            path.pop_back();
        }
        return false;
    }

    /// Try to type `target` using a composed sequence (dead key + base key, or Compose/Multi_key).
    bool emit_composed(fs8::xkb::keymap const &map, xkb_keysym_t const target, fs8::user_event_callback callback) {
        xkb_compose_table *table = compose_table();
        if (table == nullptr) [[unlikely]] {
            return false;
        }
        xkb_compose_state *state = xkb_compose_state_new(table, XKB_COMPOSE_STATE_NO_FLAGS);
        if (state == nullptr) [[unlikely]] {
            return false;
        }

        std::vector<xkb_keysym_t>       path;
        std::vector<xkb_keysym_t> const candidates  = collect_typable_keysyms(map.get());
        int                             feed_budget = 100'000;
        bool const                      found       = find_composed(state, target, candidates, path, 1, feed_budget);
        xkb_compose_state_unref(state);
        if (!found) {
            return false;
        }
        for (xkb_keysym_t const key : path) {
            if (!emit_keysym(map, key, callback)) {
                return false;
            }
        }
        return true;
    }

    /// Encode a code point as UTF-8 (empty string for invalid or control characters).
    std::string utf8_from_ucs32(char32_t const ucs32) {
        if (ucs32 < 0x20 || ucs32 > 0x10'FFFF) {
            return {};
        }
        if (ucs32 <= 0x7F) {
            return {static_cast<char>(ucs32)};
        }
        std::string out;
        if (ucs32 <= 0x7FF) {
            out += static_cast<char>(0xC0 | (ucs32 >> 6));
            out += static_cast<char>(0x80 | (ucs32 & 0x3F));
        } else if (ucs32 <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (ucs32 >> 12));
            out += static_cast<char>(0x80 | ((ucs32 >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (ucs32 & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (ucs32 >> 18));
            out += static_cast<char>(0x80 | ((ucs32 >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((ucs32 >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (ucs32 & 0x3F));
        }
        return out;
    }

    /// Comma-separated list of the layout names compiled into this keymap.
    std::string layout_names(xkb_keymap *keymap) {
        auto const count = xkb_keymap_num_layouts(keymap);
        if (count == 0) {
            return "<none>";
        }
        std::string out;
        for (xkb_layout_index_t i = 0; i < count; ++i) {
            if (i > 0) {
                out += ", ";
            }
            char const *const name  = xkb_keymap_layout_get_name(keymap, i);
            out                    += name != nullptr ? name : "?";
        }
        return out;
    }

    /// Characters that can't be produced by any keyboard layout and need an IME.
    [[nodiscard]] bool needs_ime(char32_t const ucs32) noexcept {
        return (ucs32 >= 0x3400 && ucs32 <= 0x4DBF)        // CJK Ext. A
               || (ucs32 >= 0x4E00 && ucs32 <= 0x9FFF)     // CJK Unified Ideographs
               || (ucs32 >= 0x2'0000 && ucs32 <= 0x2'A6DF) // CJK Ext. B
               || (ucs32 >= 0x3040 && ucs32 <= 0x30FF)     // Hiragana / Katakana
               || (ucs32 >= 0xAC00 && ucs32 <= 0xD7AF);    // Hangul syllables
    }

    /// Explain why a character can't be typed and how to make it typable.
    void warn_untypable(fs8::xkb::keymap const &map, char32_t const ucs32) {
        std::string const glyph   = utf8_from_ucs32(ucs32);
        std::string const layouts = layout_names(map.get());

        fs8::log("Warning: no way to type '{}' (U+{:04X}) with the current keyboard layouts: {}",
                 glyph.empty() ? "?" : glyph,
                 static_cast<uint32_t>(ucs32),
                 layouts);

        if (needs_ime(ucs32)) {
            fs8::log("  Hint: CJK (Chinese/Japanese/Korean) characters aren't on any keyboard layout;");
            fs8::log("  Hint: type them with an input method (IME) such as ibus or fcitx5,");
            fs8::log("  Hint: then switch to it while typing (e.g. with the configured hotkey).");
            return;
        }

        fs8::log("  Hint: this character isn't present on the configured layouts. To make it typable,");
        fs8::log("  Hint: add a layout that contains it (e.g. `localectl set-x11-keymap us,ir`),");
        fs8::log("  Hint: set XKB_DEFAULT_LAYOUT=us,ir, or edit /etc/default/keyboard, then re-run.");
    }
} // namespace

void fs8::xkb::how2type::emit(keymap const &map, char32_t const ucs32, user_event_callback callback) {
    // Convert Unicode -> keysym (uses libxkbcommon helper)
    xkb_keysym_t const ks = xkb_utf32_to_keysym(static_cast<uint32_t>(ucs32));
    if (ks == XKB_KEY_NoSymbol) {
        return;
    }

    // 1. Direct single-key typing
    if (emit_keysym(map, ks, callback)) {
        return;
    }

    // 2. Composed sequences (dead key + base key, or Compose/Multi_key)
    if (emit_composed(map, ks, callback)) {
        return;
    }

    // 3. Give up (e.g. a CJK character, an emoji, or a character with no keysym/sequence on this keymap)
    warn_untypable(map, ucs32);
}

void fs8::xkb::how2type::emit(keymap const &map, std::u32string_view const str, user_event_callback callback) {
    for (char32_t const ucs32 : str) {
        emit(map, ucs32, callback);
    }
}

void fs8::xkb::how2type::emit(keymap const &map, std::u8string_view const str, user_event_callback callback) {
    std::string_view const as_bytes{reinterpret_cast<char const *>(str.data()), str.size()};
    emit(map, as_bytes, callback);
}

void fs8::xkb::how2type::emit(keymap const &map, std::string_view str, user_event_callback callback) {
    while (!str.empty()) {
        char32_t const ucs32 = utf8_next_code_point(str);
        emit(map, ucs32, callback);
    }
}

void fs8::xkb::how2type::print(keymap const &map, std::string_view const str, output_syntax const syntax) {
    using enum output_syntax;

    auto const on_event = [syntax](user_event const &usr_event) {
        event_type event{usr_event};
        event.reset_time();

        auto const time = std::chrono::duration<double>(event.micro_time()).count();
        if (is_syn(event)) {
            if (syntax == evtest) {
                std::println("Event: time {:.6f}, -------------- SYN_REPORT ------------", time);
            }
            return;
        }

        switch (syntax) {
            case evtest: {
                std::println(
                  "Event: time {:.6f}, type {} ({}), code {} ({}), value {}",
                  time,
                  event.type(),
                  event.type_name(),
                  event.code(),
                  event.code_name(),
                  event.value());
                break;
            }
            case cpp_code: {
                auto const  type_name_view = event.type_name();
                std::string type_name{type_name_view.data(), type_name_view.size()};
                if (type_name.empty()) {
                    type_name = std::format("{}", event.type());
                }

                auto const  code_name_view = event.code_name();
                std::string code_name{code_name_view.data(), code_name_view.size()};
                if (code_name.empty()) {
                    code_name = std::format("{}", event.code());
                }
                std::println("{{.type = {}, .code = {}, .value = {}}},", type_name, code_name, event.value());
                break;
            }
            default: log("Invalid syntax provided."); break;
        }
    };

    // Walk the string, emitting plain text and modifier tags (e.g. "<ctrl-r>") the same way the
    // typer module does at runtime, so the output matches what real emission produces.
    std::size_t index = 0;
    for (;;) {
        std::size_t lhsptr = 0;
        std::size_t rhsptr = 0;
        if (!find_modifier_tag(str, index, lhsptr, rhsptr)) {
            break;
        }
        emit(map, str.substr(index, lhsptr - index), on_event);
        auto const tag = str.substr(lhsptr, rhsptr - lhsptr);
        if (!parse_modifier(tag, on_event)) {
            emit(map, tag, on_event);
        }
        index = rhsptr;
    }
    emit(map, str.substr(index), on_event);
}

void fs8::xkb::how2type::print(std::string_view const str, output_syntax const syntax) {
    print(get_default_keymap(), str, syntax);
}
