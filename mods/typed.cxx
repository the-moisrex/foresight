// Created by moisrex on 10/28/25.

module;
#include <array>
#include <cstdint>
#include <cstring>
#include <queue>
#include <string>
module foresight.mods.typed;
import foresight.lib.xkb.how2type;
import foresight.main.log;
import foresight.utils.hash;
import foresight.mods.event;
import foresight.lib.mod_parser;

using foresight::basic_typed;

// NOLINTBEGIN(*-pro-bounds-constant-array-index)
std::uint32_t foresight::event_search_engine::build_machine() {
    std::uint32_t last_state = 1;
    trie.resize(1);
    trie[0].fill(-1);
    output_links.resize(1, 0);
    failure_links.resize(1, 0); // Root failure points to itself

    // Insert patterns into the trie
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        auto const &word    = patterns[i];
        int         current = 0;
        for (char32_t const c : word) {
            auto const ch = static_cast<std::size_t>(c - U'a');
            if (trie[current][ch] == -1) {
                trie[current][ch] = static_cast<int>(last_state);
                auto &back        = trie.emplace_back();
                back.fill(-1);
                output_links.emplace_back(0);
                failure_links.emplace_back(0); // Temporary
                ++last_state;
            }
            current = trie[current][ch];
        }
        output_links[current] |= 1 << i;
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
                auto fail = failure_links[cstate];
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

foresight::event_search_engine::event_search_engine() {
    build_machine();
}

void foresight::event_search_engine::add_pattern(std::string_view pattern) {
    std::u32string encoded_pattern;
    encoded_pattern.reserve(pattern.size());

    // Convert to UTF-32 string
    while (!pattern.empty()) {
        char32_t const code_point = parse_char_or_codepoint(pattern);
        if (code_point == invalid_code_point) {
            log("ERROR: Invalid Code Point was found.");
            continue;
        }
        encoded_pattern += code_point;
    }
    patterns.emplace_back(std::move(encoded_pattern));

    trie.clear();
    output_links.clear();
    failure_links.clear();
    build_machine();
}

foresight::aho_state foresight::event_search_engine::process(
  char32_t const  code_point,
  aho_state const last_state) const noexcept {
    auto       state = last_state.index();
    auto const ch    = static_cast<std::size_t>(code_point - 'a');
    while (trie[state][ch] == -1) {
        state = failure_links[state];
    }
    auto const next_generation = static_cast<std::uint8_t>(last_state.generation() + 1U);
    return aho_state{static_cast<std::uint32_t>(trie[state][ch]), next_generation};
}

std::vector<std::u32string> foresight::event_search_engine::matches(std::uint32_t const state) const {
    std::vector<std::u32string> matches;
    auto const                  mask = output_links[state];
    for (std::size_t j = 0; j < patterns.size(); ++j) {
        if ((mask & (1U << j)) != 0u) {
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
