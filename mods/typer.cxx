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

    // Case-insensitive FNV-1a hash (operates on char32_t, folds ASCII A-Z->a-z while hashing)
    [[nodiscard]] constexpr std::uint64_t ci_hash(std::u32string_view const s) noexcept {
        constexpr std::uint64_t offset_basis = 14'695'981'039'346'656'037ull;
        constexpr std::uint64_t prime        = 1'099'511'628'211ull;
        std::uint64_t           h            = offset_basis;
        for (char32_t const ch : s) {
            char32_t c = ch;
            if (c >= U'A' && c <= U'Z') {
                c = c + (U'a' - U'A');
            }
            // mix the 32-bit char into the 64-bit hash as 4 separate bytes (stable)
            auto const val = static_cast<std::uint32_t>(c);
            for (int byte = 0; byte < 4; ++byte) {
                unsigned char b  = static_cast<unsigned char>((val >> (byte * 8)) & 0xFFu);
                h               ^= static_cast<std::uint64_t>(b);
                h               *= prime;
            }
        }
        return h;
    }

    // tiny helper to check prefix "left"/"right" in ASCII-only case-insensitive fashion
    [[nodiscard]] constexpr bool icontains_simple_prefix(std::u32string_view s,
                                                         std::u32string_view prefix) noexcept {
        if (s.size() < prefix.size()) {
            return false;
        }
        for (size_t i = 0; i < prefix.size(); ++i) {
            char32_t a = s[i];
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

    struct ModEntry {
        std::u32string_view key;  // must refer to a static literal (we use U"...")
        std::uint64_t       hash; // precomputed ci_hash(key)
        user_event          ev;
    };

    // Table of known names/synonyms. Add entries here as needed.
    // NOTE: keep keys lowercase where possible for readability — hash is case-insensitive anyway.
    constexpr std::array<ModEntry, 53> mod_table = []() constexpr {
        // We can't use a dynamic initializer size easily in constexpr lambda,
        // so create a temporary array with the exact number of entries.

        // NOLINTBEGIN(*-use-designated-initializers)
        return std::array<ModEntry, 53>{
          {// SHIFT
           {U"shift", ci_hash(U"shift"), foresight::left_shift},
           {U"sh", ci_hash(U"sh"), foresight::left_shift},
           {U"s", ci_hash(U"s"), foresight::left_shift},
           {U"+", ci_hash(U"+"), foresight::left_shift},
           {U"⇧", ci_hash(U"⇧"), foresight::left_shift},
           {U"leftshift", ci_hash(U"leftshift"), foresight::left_shift},
           {U"rightshift", ci_hash(U"rightshift"), foresight::right_shift},
           {U"rshift", ci_hash(U"rshift"), foresight::right_shift},
           {U"lshift", ci_hash(U"lshift"), foresight::left_shift},

           // CTRL
           {U"ctrl", ci_hash(U"ctrl"), foresight::left_ctrl},
           {U"control", ci_hash(U"control"), foresight::left_ctrl},
           {U"ctl", ci_hash(U"ctl"), foresight::left_ctrl},
           {U"c", ci_hash(U"c"), foresight::left_ctrl},
           {U"^", ci_hash(U"^"), foresight::left_ctrl},
           {U"⌃", ci_hash(U"⌃"), foresight::left_ctrl},
           {U"leftctrl", ci_hash(U"leftctrl"), foresight::left_ctrl},
           {U"rightctrl", ci_hash(U"rightctrl"), foresight::right_ctrl},
           {U"rctrl", ci_hash(U"rctrl"), foresight::right_ctrl},
           {U"lctrl", ci_hash(U"lctrl"), foresight::left_ctrl},

           // META / CMD / SUPER / WIN
           {U"meta", ci_hash(U"meta"), foresight::left_meta},
           {U"cmd", ci_hash(U"cmd"), foresight::left_meta},
           {U"command", ci_hash(U"command"), foresight::left_meta},
           {U"super", ci_hash(U"super"), foresight::left_meta},
           {U"win", ci_hash(U"win"), foresight::left_meta},
           {U"windows", ci_hash(U"windows"), foresight::left_meta},
           {U"⊞", ci_hash(U"⊞"), foresight::left_meta},
           {U"⌘", ci_hash(U"⌘"), foresight::left_meta},
           {U"leftmeta", ci_hash(U"leftmeta"), foresight::left_meta},
           {U"rightmeta", ci_hash(U"rightmeta"), foresight::right_meta},

           // ALT / OPTION / ALTGR
           {U"alt", ci_hash(U"alt"), foresight::left_alt},
           {U"option", ci_hash(U"option"), foresight::left_alt},
           {U"opt", ci_hash(U"opt"), foresight::left_alt},
           {U"a", ci_hash(U"a"), foresight::left_alt},
           {U"⌥", ci_hash(U"⌥"), foresight::left_alt},
           {U"altgr", ci_hash(U"altgr"), foresight::right_alt},
           {U"alt-gr", ci_hash(U"alt-gr"), foresight::right_alt},
           {U"leftalt", ci_hash(U"leftalt"), foresight::left_alt},
           {U"rightalt", ci_hash(U"rightalt"), foresight::right_alt},

           // LOCKS
           {U"caps", ci_hash(U"caps"), foresight::capslock},
           {U"capslock", ci_hash(U"capslock"), foresight::capslock},
           {U"num", ci_hash(U"num"), foresight::numlock},
           {U"numlock", ci_hash(U"numlock"), foresight::numlock},
           {U"scroll", ci_hash(U"scroll"), foresight::scrolllock},
           {U"scrolllock", ci_hash(U"scrolllock"), foresight::scrolllock},

           // XKB-style mod names
           {U"mod1", ci_hash(U"mod1"), foresight::left_alt},
           {U"mod2", ci_hash(U"mod2"), foresight::left_alt},
           {U"mod3", ci_hash(U"mod3"), foresight::left_alt},
           {U"mod4", ci_hash(U"mod4"), foresight::left_meta},
           {U"mod5", ci_hash(U"mod5"), foresight::left_alt},

           // some combined/alternate spellings
           {U"altgr", ci_hash(U"altgr"), foresight::right_alt},
           {U"optionkey", ci_hash(U"optionkey"), foresight::left_alt},
           {U"controlkey", ci_hash(U"controlkey"), foresight::left_ctrl}}
        };
        // NOLINTEND(*-use-designated-initializers)
    }();

    // No allocations, simple hash+compare lookup
    [[nodiscard]] constexpr user_event convert_modifier(std::u32string_view const str) noexcept {
        if (str.empty()) [[unlikely]] {
            return foresight::invalid_user_event;
        }

        std::uint64_t const hid = ci_hash(str);

        // probe over table entries, hash-first to avoid expensive compares
        for (auto const &[key, hash, ev] : mod_table) {
            if (hash != hid) {
                continue;  // cheap filter
            }
            if (iequals(key, str)) {
                return ev; // confirm with case-insensitive exact match
            }
        }

        if (str.size() <= 1) [[unlikely]] {
            return foresight::invalid_user_event;
        }

        // Attempt a small additional optimization: look for common prefixes like "r" or "l" + keyword
        // e.g., user may pass "rctrl" or "left-ctrl" forms not present exactly — handle some fast patterns:
        // detect leading 'r' / 'l' or "right"/"left" and re-run search on remainder
        auto const first = str[0];
        if (first == U'r' || first == U'R' || first == U'l' || first == U'L') {
            auto const remainder = str.substr(1);
            auto const hr        = ci_hash(remainder);
            for (auto const &[key, hash, ev] : mod_table) {
                if (hash != hr) {
                    continue;
                }
                if (iequals(key, remainder)) {
                    // map based on leading letter
                    if (first == U'r' || first == U'R') {
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
                    } else {
                        // 'l' => left (the table already maps many to left)
                        return ev;
                    }
                }
            }
        }

        // check "right" / "left" spelled out prefixes
        if (str.size() <= 5) [[unlikely]] {
            return foresight::invalid_user_event;
        }
        // cheap check for "right..." or "left..."
        auto const c0 = str[0];
        if ((c0 == U'r' || c0 == U'R') && (str.size() >= 5)) {
            auto const possible = str;
            if (icontains_simple_prefix(possible, U"right")) {
                auto const remainder = possible.substr(5);
                auto const hr        = ci_hash(remainder);
                for (auto const &[key, hash, ev] : mod_table) {
                    if (hash != hr) {
                        continue;
                    }
                    if (iequals(key, remainder)) {
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
                }
            }
        } else if ((c0 == U'l' || c0 == U'L') && (str.size() >= 4)) {
            auto const possible = str;
            if (icontains_simple_prefix(possible, U"left")) {
                auto const remainder = possible.substr(4);
                auto const hr        = ci_hash(remainder);
                for (auto const &[key, hash, ev] : mod_table) {
                    if (hash != hr) {
                        continue;
                    }
                    if (iequals(key, remainder)) {
                        return ev;
                    }
                }
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

    struct [[nodiscard]] modifier_info {
        std::u32string_view mod_str;
        std::u32string_view key_str;
        bool                is_release   = false;
        bool                is_monotonic = false;
    };

    constexpr modifier_info parse_mod(std::u32string_view mod_str) noexcept {
        bool const    is_release   = mod_str.starts_with(U'/');
        auto const    dash_start   = mod_str.find(U'-');
        bool const    is_monotonic = !is_release && dash_start != std::u32string_view::npos;
        modifier_info info{.mod_str      = {},
                           .key_str      = {},
                           .is_release   = is_release,
                           .is_monotonic = is_monotonic};

        // handling </shift> or </ctrl>
        if (is_release) {
            mod_str.remove_prefix(1);
        }

        auto const mod_substr = mod_str.substr(0, dash_start);
        info.mod_str          = mod_substr;
        if (!is_monotonic) {
            auto const key_str = mod_str.substr(dash_start + 1);
            info.key_str       = key_str;
        }
        return info;
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
        auto const rhs    = str.substr(lhsptr + 1, rhsptr - lhsptr - 1);

        // 2. parse the modifier string if any:
        auto const info = parse_mod(rhs);
        auto const mod  = convert_modifier(info.mod_str);

        // 3. convert the modifiers as well:
        if (mod == invalid_user_event) [[unlikely]] {
            auto const both = str.substr(0, rhsptr + 1);
            this->typer->how(both, emit_event);
        } else {
            // emit the events:
            this->typer->how(lhs, emit_event);
            if (info.is_release) {
                auto mod_released  = auto{mod};
                mod_released.value = 0;
                emit_event(mod_released);
            } else if (info.is_monotonic) {
                emit_event(mod);
                auto mod_released  = auto{mod};
                mod_released.value = 0;
                emit_event(mod_released);
            } else {
                emit_event(mod);
            }
        }

        // 4. remove the already processed string:
        str.remove_prefix(rhsptr);
    }
}
