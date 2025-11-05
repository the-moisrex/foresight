// Created by moisrex on 11/4/25.

module;
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <stdexcept>
module foresight.lib.mod_parser;
import foresight.mods.event;
import foresight.devices.key_codes;
import foresight.utils.hash;
import foresight.main.log;

using foresight::user_event;

// NOLINTBEGIN(*-magic-numbers)
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

    /**
     * Array mapping the leading byte to the length of a UTF-8 sequence.
     * A value of zero indicates that the byte can not begin a UTF-8 sequence.
     */
    constexpr std::array<uint8_t, 256> utf8_sequence_length_by_leading_byte{
      {
       1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x00-0x0F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x10-0x1F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x20-0x2F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x30-0x3F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x40-0x4F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x50-0x5F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x60-0x6F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x70-0x7F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x80-0x8F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x90-0x9F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xA0-0xAF */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xB0-0xBF */
        0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 0xC0-0xCF */
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 0xD0-0xDF */
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 0xE0-0xEF */
        4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xF0-0xFF */
      }
    };

    /// Length of next utf-8 sequence
    std::uint8_t utf8_sequence_length(char const src) noexcept {
        return utf8_sequence_length_by_leading_byte.at(static_cast<unsigned char>(src));
    }

    /**
     * Check if a Unicode code point is a surrogate.
     * Those code points are used only in UTF-16 encodings.
     */
    [[nodiscard]] bool is_surrogate(char32_t const cp) noexcept {
        return cp >= 0xd800 && cp <= 0xDFFFU;
    }

    [[nodiscard]] bool is_empty(char const *src) noexcept {
        return src == nullptr || *src == '\0';
    }

    // ASCII-only case-insensitive equality (keeps your original semantics)
    [[nodiscard]] bool iequals(std::u32string_view const lhs, std::u32string_view const rhs) noexcept {
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
            field.hash = foresight::ci_hash(field.key);
        }
        return data;
        // NOLINTEND(*-use-designated-initializers)
    }();

    /**
     * Alternative keys
     * This function finds alternative representations of common keys.
     */
    user_event alternative_modifier(std::u32string_view const str) noexcept {
        auto const hid = foresight::ci_hash(str);

        // probe over table entries, hash-first to avoid expensive compares
        for (auto const &[key, ev, hash] : mod_table) {
            if (hash != hid || !iequals(key, str)) {
                continue; // cheap filter
            }
            return ev;    // confirm with case-insensitive exact match
        }

        [[unlikely]] { return foresight::invalid_user_event; }
    }

    /// Convert key names or single characters to user_event (with value=1)
    user_event to_event(std::u32string_view const key) noexcept {
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

    [[nodiscard]] constexpr foresight::code32_t to_code(std::u32string_view const key) noexcept {
        // todo: implement
        return foresight::invalid_code_point;
    }

} // namespace

/// Reads the next UTF-8 sequence in a string
char32_t foresight::utf8_next_code_point(std::string_view &src, std::size_t const max_size) noexcept {
    char32_t   code_point = 0;
    auto const len        = utf8_sequence_length(src.front());

    if (max_size == 0u || len > max_size) {
        return invalid_code_point;
    }

    // Handle leading byte
    auto const src0 = src[0];
    switch (len) {
        case 1: src.remove_prefix(1); return static_cast<char32_t>(src0);
        case 2: code_point = static_cast<char32_t>(src0) & 0x1FU; break;
        case 3: code_point = static_cast<char32_t>(src0) & 0x0FU; break;
        case 4: code_point = static_cast<char32_t>(src0) & 0x07U; break;
        default: return invalid_code_point;
    }

    // Process remaining bytes of the UTF-8 sequence
    for (std::size_t k = 1; k < len; k++) {
        if ((static_cast<char32_t>(src[k]) & 0xC0U) != 0x80U) {
            return invalid_code_point;
        }
        code_point <<= 6U;
        code_point  |= static_cast<char32_t>(src[k]) & 0x3FU;
    }

    src.remove_prefix(len);

    // Check surrogates
    if (is_surrogate(code_point)) {
        return invalid_code_point;
    }

    return code_point;
}

char32_t foresight::parse_char_or_codepoint(std::string_view &src) noexcept {
    auto const raw_length = src.size();

    if (raw_length == 0U) {
        return invalid_code_point;
    }

    // Try to parse the parameter as a UTF-8 encoded single character
    char32_t   codepoint = utf8_next_code_point(src, raw_length);
    auto const length    = src.size() - raw_length;

    // If parsing failed or did not consume all the string, then try other formats
    if (codepoint == invalid_code_point || length == 0 || length != raw_length) {
        char    *endp = nullptr;
        char32_t val  = 0;
        int      base = 10;
        // Detect U+NNNN format standard Unicode code point format
        if (src.starts_with("U+")) {
            base = 16;
            src.remove_prefix(2);
        }
        // Use strtol with explicit bases instead of `0` in order to avoid unexpected parsing as octal.
        for (; base <= 16; base += 6) {
            errno = 0;
            val   = static_cast<char32_t>(strtol(src.data(), &endp, base));
            if (errno != 0 || !is_empty(endp) || static_cast<std::int32_t>(val) < 0 || val > 0x10'FFFFU) {
                val = invalid_code_point;
            } else {
                break;
            }
        }
        if (val == invalid_code_point) {
            log("ERROR: Failed to convert to UTF-32");
            return invalid_code_point;
        }
        src.remove_prefix(endp - src.data());
        codepoint = val;
    }
    return codepoint;
}

std::size_t
foresight::find_delim(std::u32string_view str, char32_t const delim, std::size_t const pos) noexcept {
    auto lhsptr = str.find(delim, pos);
    while (lhsptr != 0 && str.at(lhsptr - 1) != U'\\') {
        // skip the escaped ones
        lhsptr = str.find(delim, lhsptr + 1);
    }
    return lhsptr;
}

bool foresight::parse_modifier(std::u32string_view mod_str, user_event_callback callback) {
    assert(mod_str.starts_with(U'<') && mod_str.ends_with(U'>'));
    mod_str.remove_prefix(1);
    mod_str.remove_suffix(1);
    bool const is_release   = mod_str.starts_with(U'/');
    auto       dash_start   = mod_str.find(U'-');
    bool const is_monotonic = !is_release && dash_start != std::u32string_view::npos;
    user_event event;

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
        constexpr auto max_len = static_cast<std::int32_t>(max_simultaneous_key_presses);
        std::array<std::uint16_t, max_simultaneous_key_presses> keys{};
        std::int32_t                                            index = 0;
        for (; index != max_len; ++index) {
            auto const sub_mod = mod_str.substr(dash_start);
            if (sub_mod.empty()) {
                break;
            }
            auto const ev = to_event(sub_mod);
            if (is_invalid(ev)) [[unlikely]] {
                break;
            }
            keys.at(index) = ev.code;
            callback(ev); // keydown
            callback(syn_user_event);
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
            callback(syn_user_event);
        }
        return keys.front() != 0;
    }

    event.value = 1;
    callback(event);
    callback(syn_user_event);
    event.value = 0;
    callback(event);
    callback(syn_user_event);
    return true;
}

foresight::code32_t foresight::parse_next_code_point(std::u32string_view &str) noexcept {
    if (str.empty()) [[unlikely]] {
        return U'\0';
    }
    if (str.front() != U'<') {
        auto const code_point = str.front();
        str.remove_prefix(1);
        return code_point;
    }

    // find the first modifier:
    auto const rhsptr = find_delim(str, U'>');
    auto const code   = str.substr(0, rhsptr);

    // remove the already processed string:
    str.remove_prefix(rhsptr);

    return to_code(code);
}

// NOLINTEND(*-magic-numbers)
