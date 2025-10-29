// Created by moisrex on 10/11/25.

module;
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <ranges>
#include <stdexcept>
#include <string_view>
module foresight.mods.typer;
import foresight.devices.key_codes;

using foresight::user_event;

namespace foresight {
    constexpr std::size_t max_simultaneous_key_presses = 32;

    constexpr user_event left_shift{.type = EV_KEY, .code = KEY_LEFTSHIFT, .value = 1};
    constexpr user_event right_shift{.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 1};

    constexpr user_event left_ctrl{.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 1};
    constexpr user_event right_ctrl{.type = EV_KEY, .code = KEY_RIGHTCTRL, .value = 1};

    constexpr user_event left_meta{.type = EV_KEY, .code = KEY_LEFTMETA, .value = 1};
    constexpr user_event right_meta{.type = EV_KEY, .code = KEY_RIGHTMETA, .value = 1};

    constexpr user_event left_alt{.type = EV_KEY, .code = KEY_LEFTALT, .value = 1};
    constexpr user_event right_alt{.type = EV_KEY, .code = KEY_RIGHTALT, .value = 1};

    constexpr user_event capslock{.type = EV_KEY, .code = KEY_CAPSLOCK, .value = 1};
    constexpr user_event numlock{.type = EV_KEY, .code = KEY_NUMLOCK, .value = 1};
    constexpr user_event scrolllock{.type = EV_KEY, .code = KEY_SCROLLLOCK, .value = 1};

} // namespace foresight

namespace {
    // ASCII-only case-insensitive equality (keeps your original semantics)
    [[nodiscard]] constexpr bool iequals(std::u32string_view const lhs,
                                         std::u32string_view const rhs) noexcept {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        static constexpr auto to_lower = [](char32_t const ch32) constexpr noexcept -> char32_t {
            if (ch32 >= U'A' && ch32 <= U'Z') {
                return ch32 + (U'a' - U'A');
            }
            return ch32;
        };

        return std::ranges::equal(lhs, rhs, {}, to_lower, to_lower);
    }

    // 32-bit FNV-1a constants
    constexpr uint32_t FNV1A_32_INIT  = 0x811C'9DC5U;
    constexpr uint32_t FNV1A_32_PRIME = 0x0100'0193U;

    // 64-bit FNV-1a constants
    [[maybe_unused]] constexpr uint64_t FNV1A_64_INIT  = 0xCBF2'9CE4'8422'2325ULL;
    [[maybe_unused]] constexpr uint64_t FNV1A_64_PRIME = 0x100'0000'01B3ULL;

    /**
     * Byte Hashing - FNV-1a
     * Case-insensitive FNV-1a hash (operates on char32_t, folds ASCII A-Z->a-z while hashing)
     * std::hash<std::uint32_string_view> is not constexpr, so we implement our own.
     * http://www.isthe.com/chongo/tech/comp/fnv/
     */
    [[nodiscard]] constexpr std::uint32_t ci_hash(std::u32string_view const src) noexcept {
        std::uint32_t hash = FNV1A_32_INIT;
        for (char32_t c : src) {
            if (c >= U'A' && c <= U'Z') {
                c = c + (U'a' - U'A');
            }
            hash ^= static_cast<std::uint32_t>(c);
            hash *= FNV1A_32_PRIME;
        }
        return hash;
    }

    // tiny helper to check prefix "left"/"right" in ASCII-only case-insensitive fashion
    [[nodiscard]] constexpr bool icontains_simple_prefix(std::u32string_view const src,
                                                         std::u32string_view const prefix) noexcept {
        if (src.size() < prefix.size()) {
            return false;
        }
        for (size_t i = 0; i < prefix.size(); ++i) {
            char32_t a = src[i];
            char32_t b = prefix[i];
            if (a >= U'A' && a <= U'Z') {
                a = a + (U'a' - U'A');
            }
            if (b >= U'A' && b <= U'Z') {
                b = b + (U'a' - U'A');
            }
            if (a != b) {
                return false;
            }
        }
        return true;
    }

    struct mod_entry {
        std::u32string_view key;      // must refer to a static literal (we use U"...")
        user_event          ev;
        std::uint32_t       hash = 0; // precomputed ci_hash(key)
    };

    // Table of known names/synonyms. Add entries here as needed.
    // NOTE: keep keys lowercase where possible for readability — hash is case-insensitive anyway.
    constexpr std::array<mod_entry, 53> mod_table = []() consteval {
        // NOLINTBEGIN(*-use-designated-initializers)
        std::array<mod_entry, 53> data{
          {// SHIFT
           {U"shift", foresight::left_shift},
           {U"sh", foresight::left_shift},
           {U"s", foresight::left_shift},
           {U"+", foresight::left_shift},
           {U"⇧", foresight::left_shift},
           {U"leftshift", foresight::left_shift},
           {U"rightshift", foresight::right_shift},
           {U"rshift", foresight::right_shift},
           {U"lshift", foresight::left_shift},

           // CTRL
           {U"ctrl", foresight::left_ctrl},
           {U"control", foresight::left_ctrl},
           {U"ctl", foresight::left_ctrl},
           {U"c", foresight::left_ctrl},
           {U"^", foresight::left_ctrl},
           {U"⌃", foresight::left_ctrl},
           {U"leftctrl", foresight::left_ctrl},
           {U"rightctrl", foresight::right_ctrl},
           {U"rctrl", foresight::right_ctrl},
           {U"lctrl", foresight::left_ctrl},

           // META / CMD / SUPER / WIN
           {U"meta", foresight::left_meta},
           {U"cmd", foresight::left_meta},
           {U"command", foresight::left_meta},
           {U"super", foresight::left_meta},
           {U"win", foresight::left_meta},
           {U"windows", foresight::left_meta},
           {U"⊞", foresight::left_meta},
           {U"⌘", foresight::left_meta},
           {U"leftmeta", foresight::left_meta},
           {U"rightmeta", foresight::right_meta},

           // ALT / OPTION / ALTGR
           {U"alt", foresight::left_alt},
           {U"option", foresight::left_alt},
           {U"opt", foresight::left_alt},
           {U"a", foresight::left_alt},
           {U"⌥", foresight::left_alt},
           {U"altgr", foresight::right_alt},
           {U"alt_gr", foresight::right_alt},
           {U"leftalt", foresight::left_alt},
           {U"rightalt", foresight::right_alt},

           // LOCKS
           {U"caps", foresight::capslock},
           {U"capslock", foresight::capslock},
           {U"num", foresight::numlock},
           {U"numlock", foresight::numlock},
           {U"scroll", foresight::scrolllock},
           {U"scrolllock", foresight::scrolllock},

           // XKB-style mod names
           {U"mod1", foresight::left_alt},
           {U"mod2", foresight::left_alt},
           {U"mod3", foresight::left_alt},
           {U"mod4", foresight::left_meta},
           {U"mod5", foresight::left_alt},

           // some combined/alternate spellings
           {U"altgr", foresight::right_alt},
           {U"optionkey", foresight::left_alt},
           {U"controlkey", foresight::left_ctrl}}
        };
        for (auto &field : data) {
            field.hash = ci_hash(field.key);
        }
        return data;
        // NOLINTEND(*-use-designated-initializers)
    }();

    /**
     * Alternative keys
     * This function finds alternative representations of common keys.
     */
    constexpr user_event alternative_modifier(std::u32string_view const str) noexcept {
        auto const hid = ci_hash(str);

        // probe over table entries, hash-first to avoid expensive compares
        for (auto const &[key, ev, hash] : mod_table) {
            if (hash != hid || !iequals(key, str)) {
                continue; // cheap filter
            }
            return ev;    // confirm with case-insensitive exact match
        }

        [[unlikely]] { return foresight::invalid_user_event; }
    }

    /// Find the specified delimiter, but also checks if it's escaped or not.
    constexpr std::size_t
    find_delim(std::u32string_view str, char32_t const delim, std::size_t const pos = 0) noexcept {
        auto lhsptr = str.find(delim, pos);
        while (lhsptr != 0 && str.at(lhsptr - 1) != U'\\') {
            // skip the escaped ones
            lhsptr = str.find(delim, lhsptr + 1);
        }
        return lhsptr;
    }

    /// Convert key names or single characters to user_event (with value=1)
    constexpr user_event to_event(std::u32string_view const key) noexcept {
        auto const code = foresight::key_code_of(key);
        if (code == 0) {
            return alternative_modifier(key);
        }
        return user_event{
          .type  = EV_KEY,
          .code  = code,
          .value = 1,
        };
    }

    /// Parse a string that starts with `<` and ends with `>`, call the callback function on them.
    [[nodiscard]] constexpr bool parse_mod(std::u32string_view            mod_str,
                                           foresight::user_event_callback callback) noexcept {
        assert(mod_str.starts_with(U'<') && mod_str.ends_with(U'>'));
        mod_str.remove_prefix(1);
        mod_str.remove_suffix(1);
        bool const is_release   = mod_str.starts_with(U'/');
        auto       dash_start   = mod_str.find(U'-');
        bool const is_monotonic = !is_release && dash_start != std::u32string_view::npos;
        user_event event{};

        if (is_monotonic) {
            // handling <shift> or <ctrl> types
            event = to_event(mod_str);
        } else if (is_release) {
            // handling </shift> or </ctrl>
            mod_str.remove_prefix(1);
            event       = to_event(mod_str);
            event.value = 0;
        } else {
            // handling <C-r> type of mods
            constexpr auto max_len = static_cast<std::int32_t>(foresight::max_simultaneous_key_presses);
            std::array<std::uint16_t, foresight::max_simultaneous_key_presses> keys{};
            std::int32_t                                                       index = 0;
            for (; index != max_len; ++index) {
                auto const sub_mod = mod_str.substr(dash_start);
                if (sub_mod.empty()) {
                    break;
                }
                auto const ev = to_event(sub_mod);
                if (foresight::is_invalid(ev)) [[unlikely]] {
                    break;
                }
                keys.at(index) = ev.code;
                callback(ev); // keydown
                callback(foresight::syn_user_event);
                mod_str.remove_prefix(dash_start + 1);
                dash_start = mod_str.find(U'-');
            }

            // release the keys in reverse order:
            for (; index >= 0; --index) {
                // keyup:
                callback(user_event{
                  .type  = EV_KEY,
                  .code  = keys.at(index),
                  .value = 0,
                });
                callback(foresight::syn_user_event);
            }
            return keys.front() != 0;
        }

        event.value = 1;
        callback(event);
        callback(foresight::syn_user_event);
        event.value = 0;
        callback(event);
        callback(foresight::syn_user_event);
        return true;
    }

} // namespace

void foresight::basic_typist::emit(std::u32string_view str, user_event_callback callback) {
    // first initialize the how2type object
    if (!typer.has_value()) {
        typer = std::make_optional<xkb::how2type>();
    }

    while (!str.empty()) {
        // 1. find the first modifier:
        auto const lhsptr = find_delim(str, U'<');
        auto const rhsptr = find_delim(str, U'>', lhsptr);
        auto const lhs    = str.substr(0, lhsptr);
        auto const rhs    = str.substr(lhsptr, rhsptr - lhsptr);

        // 2. emit the strings before the <...> mod
        this->typer->emit(lhs, callback);

        // 3. parse the modifier string if any:
        if (!parse_mod(rhs, callback)) [[unlikely]] {
            // send the <...> string as is:
            this->typer->emit(rhs, callback);
        }

        // 4. remove the already processed string:
        str.remove_prefix(rhsptr);
    }
}

void foresight::basic_typist::emit(std::u32string_view const str) {
    auto const emit_event = [&](user_event const &event) {
        events.emplace_back(event);
    };
    emit(str, emit_event);
}
