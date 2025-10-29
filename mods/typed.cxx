// Created by moisrex on 10/28/25.

module;
#include <cstdint>
#include <cstring>
#include <string>
module foresight.mod.typed;
import foresight.lib.xkb.how2type;
import foresight.main.log;
import foresight.utils.hash;

using foresight::basic_typed;

// NOLINTBEGIN(*-magic-numbers)
namespace {
    constexpr char32_t invalid_code_point = UINT32_MAX;

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
    [[nodiscard]] constexpr bool is_surrogate(char32_t const cp) noexcept {
        return cp >= 0xd800 && cp <= 0xDFFFU;
    }

    [[nodiscard]] constexpr bool isempty(char const *src) noexcept {
        return src == nullptr || src[0] == '\0';
    }

    /// Reads the next UTF-8 sequence in a string
    char32_t utf8_next_code_point(std::string_view &src, std::size_t const max_size) noexcept {
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
        for (size_t k = 1; k < len; k++) {
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

    [[nodiscard]] char32_t parse_char_or_codepoint(std::string_view &src) noexcept {
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
                if (errno != 0 || !isempty(endp) || static_cast<std::int32_t>(val) < 0 || val > 0x10'FFFFU) {
                    val = invalid_code_point;
                } else {
                    break;
                }
            }
            if (val == invalid_code_point) {
                foresight::log("ERROR: Failed to convert to UTF-32");
                return invalid_code_point;
            }
            src.remove_prefix(endp - src.data());
            codepoint = val;
        }
        return codepoint;
    }
} // namespace

// NOLINTEND(*-magic-numbers)

void basic_typed::operator()(start_tag) {
    xkb::how2type    typer;
    std::u32string   str;
    std::string_view utf8_trigger = trigger;
    str.reserve(utf8_trigger.size());
    while (!utf8_trigger.empty()) {
        char32_t const code_point = parse_char_or_codepoint(utf8_trigger);
        if (code_point == invalid_code_point) {
            log("ERROR: Invalid Code Point was found.");
            continue;
        }
        str += code_point;
    }
    fnv1a_init(target_hash);
    fnv1a_init(current_hash);
    typer.emit(str, [&](user_event const &event) {
        fnv1a_hash(target_hash, event.code);
    });
}

bool basic_typed::operator()(event_type const &event) noexcept {
    // todo: this is completely wrong:
    fnv1a_hash(target_hash, event.code());
    return target_hash == current_hash;
}
