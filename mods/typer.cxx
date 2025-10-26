// Created by moisrex on 10/11/25.

module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <ranges>
#include <stdexcept>
#include <string_view>
module foresight.mods.typer;
import foresight.devices.key_codes;

using foresight::user_event;

namespace foresight {
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
           {U"alt-gr", foresight::right_alt},
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
        if (str.empty()) [[unlikely]] {
            return foresight::invalid_user_event;
        }

        auto const hid = ci_hash(str);

        // probe over table entries, hash-first to avoid expensive compares
        for (auto const &[key, ev, hash] : mod_table) {
            if (hash != hid || !iequals(key, str)) {
                continue; // cheap filter
            }
            return ev;    // confirm with case-insensitive exact match
        }

        if (str.size() <= 1) [[unlikely]] {
            return foresight::invalid_user_event;
        }

        // Attempt a small additional optimization: look for common prefixes like "r" or "l" + keyword
        // e.g., user may pass "rctrl" or "left-ctrl" forms not present exactly — handle some fast patterns:
        // detect leading 'r' / 'l' or "right"/"left" and re-run search on remainder
        auto const first    = str[0];
        bool const is_left  = first == U'l' || first == U'L';
        bool const is_right = first == U'r' || first == U'R';
        if (is_left || is_right) {
            auto const remainder = str.substr(1);
            auto const hr        = ci_hash(remainder);
            for (auto const &[key, ev, hash] : mod_table) {
                if (hash != hr || !iequals(key, remainder)) {
                    continue;
                }
                if (first != U'r' && first != U'R') {
                    // 'l' => left (the table already maps many to left)
                    return ev;
                }
                // map based on leading letter
                // map to right variant when available
                if (ev.type == EV_KEY && ev.code == KEY_LEFTSHIFT) {
                    return foresight::right_shift;
                }
                if (ev.type == EV_KEY && ev.code == KEY_LEFTCTRL) {
                    return foresight::right_ctrl;
                }
                if (ev.type == EV_KEY && ev.code == KEY_LEFTMETA) {
                    return foresight::right_meta;
                }
                if (ev.type == EV_KEY && ev.code == KEY_LEFTALT) {
                    return foresight::right_alt;
                }
            }
        }

        // check "right" / "left" spelled out prefixes
        if (str.size() <= 5) [[unlikely]] {
            return foresight::invalid_user_event;
        }
        // cheap check for "right..." or "left..."
        if (is_right && str.size() >= 5 && icontains_simple_prefix(str, U"right")) {
            auto const remainder = str.substr(5);
            auto const hr        = ci_hash(remainder);
            for (auto const &[key, ev, hash] : mod_table) {
                if (hash != hr || !iequals(key, remainder)) {
                    continue;
                }
                // map to right variant
                if (ev.type == EV_KEY && ev.code == KEY_LEFTSHIFT) {
                    return foresight::right_shift;
                }
                if (ev.type == EV_KEY && ev.code == KEY_LEFTCTRL) {
                    return foresight::right_ctrl;
                }
                if (ev.type == EV_KEY && ev.code == KEY_LEFTMETA) {
                    return foresight::right_meta;
                }
                if (ev.type == EV_KEY && ev.code == KEY_LEFTALT) {
                    return foresight::right_alt;
                }
            }
        } else if (is_left && str.size() >= 4 && icontains_simple_prefix(str, U"left")) {
            auto const remainder = str.substr(4);
            auto const hr        = ci_hash(remainder);
            for (auto const &[key, ev, hash] : mod_table) {
                if (hash != hr || !iequals(key, remainder)) {
                    continue;
                }
                return ev;
            }
        }

        [[unlikely]] { return foresight::invalid_user_event; }
    }

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
    constexpr user_event convert_key(std::u32string_view const key) noexcept {
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

    bool parse_and_emit_keys(std::u32string_view input, void (*callback)(user_event const &)) noexcept {
        size_t pos = 0;

        // Parse <...> token
        ++pos;
        size_t                    start = pos;
        std::array<user_event, 6> mods{};
        size_t                    mod_count = 0;

        while (pos < input.size() && input[pos] != U'>') {
            if (input[pos] == U'-') {
                std::u32string_view mod_str = input.substr(start, pos - start);
                user_event          mod_ev  = alternative_modifier(mod_str);
                if (mod_ev.type == 0) {
                    return false; // Invalid modifier
                }
                if (mod_count >= mods.size()) {
                    return false; // Too many modifiers
                }
                mods[mod_count++] = mod_ev;
                ++pos;
                start = pos;
            } else {
                ++pos;
            }
        }

        if (pos >= input.size() || input[pos] != U'>') {
            return false; // Unclosed <
        }

        std::u32string_view key_str = input.substr(start, pos - start);
        ++pos; // Skip '>'

        user_event key = convert_key(key_str);
        if (key.type == 0) {
            return false; // Invalid key
        }

        // Emit presses: modifiers then key
        for (size_t i = 0; i < mod_count; ++i) {
            user_event ev = mods[i];
            ev.value      = 1;
            callback(ev);
        }
        {
            user_event ev = key;
            ev.value      = 1;
            callback(ev);
        }

        // Emit releases: key then modifiers in reverse
        {
            user_event ev = key;
            ev.value      = 0;
            callback(ev);
        }
        for (size_t i = mod_count; i-- > 0;) {
            user_event ev = mods[i];
            ev.value      = 0;
            callback(ev);
        }
        return true;
    }

    // The main function that routes the parsing to sub-parsers
    constexpr bool parse_mod(std::u32string_view mod_str, auto &callback) noexcept {
        bool const is_release   = mod_str.starts_with(U'/');
        auto const dash_start   = mod_str.find(U'-');
        bool const is_monotonic = !is_release && dash_start != std::u32string_view::npos;

        // handling </shift> or </ctrl>
        if (is_release) {
            mod_str.remove_prefix(1);
        }

        auto const mod_substr = mod_str.substr(0, dash_start);
        if (!is_monotonic) {
            auto const key_str = mod_str.substr(dash_start + 1);
        }
        return false; // todo
    }

} // namespace

void foresight::basic_typist::emit(std::u32string_view str) {
    // first initialize the how2type object
    if (!typer.has_value()) {
        typer = std::make_optional<xkb::how2type>();
    }

    auto const emit_event = [&](user_event const &event) {
        events.emplace_back(event);
    };

    while (!str.empty()) {
        // 1. find the first modifier:
        auto const lhsptr = find_delim(str, U'<');
        auto const rhsptr = find_delim(str, U'>', lhsptr);
        auto const lhs    = str.substr(0, lhsptr);
        auto const rhs    = str.substr(lhsptr, rhsptr - lhsptr);

        // 2. emit the strings before the <...> mod
        this->typer->how(lhs, emit_event);

        // 3. parse the modifier string if any:
        if (!parse_mod(rhs, emit_event)) [[unlikely]] {
            auto const both = str.substr(0, rhsptr + 1);
            this->typer->how(both, emit_event);
        }

        // 4. remove the already processed string:
        str.remove_prefix(rhsptr);
    }
}
