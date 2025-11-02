// Created by moisrex on 10/28/25.

module;
#include <array>
#include <cstdint>
#include <cstring>
#include <queue>
#include <string>
module foresight.mod.typed;
import foresight.lib.xkb.how2type;
import foresight.main.log;
import foresight.utils.hash;
import foresight.mods.event;

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
// NOLINTBEGIN(*-pro-bounds-constant-array-index)
int foresight::aho_typed_status::build_machine() {
    int last_state = 1;
    trie.resize(1);
    trie[0].fill(-1);
    output_links.resize(1, 0);
    failure_links.resize(1, 0); // Root failure points to itself

    // Insert patterns into the trie
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        auto const &word    = patterns[i];
        int         current = 0;
        for (char const c : word) {
            auto const ch = static_cast<std::size_t>(c - 'a');
            if (trie[current][ch] == -1) {
                trie[current][ch] = last_state;
                auto &back        = trie.emplace_back();
                back.fill(-1);
                output_links.emplace_back(0);
                failure_links.emplace_back(0); // Temporary
                ++last_state;
            }
            current = trie[current][ch];
        }
        output_links[current] |= 1U << i;
    }

    // Set goto for root's undefined transitions to itself
    for (std::size_t ch = 0; ch < ALPHABET_SIZE; ++ch) {
        if (trie[0][ch] == -1) {
            trie[0][ch] = 0;
        }
    }

    // Build failure links using BFS
    std::queue<int> q;
    for (std::size_t ch = 0; ch < ALPHABET_SIZE; ++ch) {
        auto const next = trie[0][ch];
        if (next != 0) {
            failure_links[next] = 0;
            q.push(next);
        }
    }

    while (!q.empty()) {
        int const cstate = q.front();
        q.pop();
        for (std::size_t ch = 0; ch < ALPHABET_SIZE; ++ch) {
            if (trie[cstate][ch] != -1) {
                int fail = failure_links[cstate];
                while (trie[fail][ch] == -1) {
                    fail = failure_links[fail];
                }
                fail                             = trie[fail][ch];
                failure_links[trie[cstate][ch]]  = fail;
                output_links[trie[cstate][ch]]  |= output_links[fail];
                q.push(trie[cstate][ch]);
            }
        }
    }
    return last_state;
}

int foresight::aho_typed_status::find_next_state(int current_state, char const input) const noexcept {
    auto const ch = static_cast<std::size_t>(input - 'a');
    while (trie[current_state][ch] == -1) {
        current_state = failure_links[current_state];
    }
    return trie[current_state][ch];
}

foresight::aho_typed_status::aho_typed_status() {
    build_machine();
}

void foresight::aho_typed_status::add_pattern(std::string_view const pattern) {
    patterns.emplace_back(pattern.data(), pattern.size());
    trie.clear();
    output_links.clear();
    failure_links.clear();
    build_machine();
}

int foresight::aho_typed_status::process(char const code_point, int const state) noexcept {
    return find_next_state(state, code_point);
}

std::vector<std::string> foresight::aho_typed_status::matches(int state) const {
    std::vector<std::string> matches;
    int const                mask         = output_links[state];
    std::size_t const        num_patterns = patterns.size();
    for (std::size_t j = 0; j < num_patterns; ++j) {
        if (mask & (1 << j)) {
            matches.push_back(patterns[j]);
        }
    }
    return matches;
}

// NOLINTEND(*-pro-bounds-constant-array-index)

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
    // Build the pattern of hashed(type, code) from the events required to type the trigger string
    target_length  = 0;
    current_length = 0;
    fnv1a_init(target_hash);
    fnv1a_init(current_hash);
    // typer.emit(str, [this](user_event const &event) noexcept {
    //     // Use the same hashing as runtime events (type+code; ignore value)
    //     ++target_length;
    //     // Keep legacy rolling hash for potential external users
    //     fnv1a_hash(target_hash, event.code);
    // });
}

bool basic_typed::operator()(event_type const &event) noexcept {
    // todo: this is completely wrong:
    fnv1a_hash(target_hash, event.code());
    return target_hash == current_hash;
}
