// Created by moisrex on 10/28/25.

module;
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <queue>
#include <string>
module foresight.mods.typed;
import foresight.lib.xkb.how2type;
import foresight.main.log;
import foresight.utils.hash;
import foresight.mods.event;
import foresight.lib.mod_parser;

using foresight::basic_search_engine;

// NOLINTBEGIN(*-pro-bounds-constant-array-index)
basic_search_engine::state_type basic_search_engine::find_child(
  state_type const state,
  char32_t const   code) const noexcept {
    auto const &node     = trie[state];
    auto const &children = node.children;

    if ((node.children_mask & code) == 0) [[likely]] {
        // User most likely won't be typing the shortcuts all the time, so we put it in the slow path
        return 0; // index to root
    }

    // binary search since children is kept sorted by codepoint
    auto const it =
      std::lower_bound(children.begin(), children.end(), code, [](auto const &a, char32_t value) {
          return a.first < value;
      });
    if (it != children.end() && it->first == code) {
        return it->second;
    }
    return 0; // index to root
}

std::uint32_t basic_search_engine::add_child(state_type const state, char32_t code, state_type child_index) {
    auto      &children = trie[state].children;
    auto const it =
      std::lower_bound(children.begin(), children.end(), code, [](auto const &a, char32_t value) {
          return a.first < value;
      });
    children.emplace(it, code, child_index); // insert sorted

    std::uint32_t mask = 0U;
    for (auto const child : children) {
        mask |= child.first;
    }
    return mask;
}

std::uint32_t basic_search_engine::build_machine() {
    trie.clear();
    auto &root     = trie.emplace_back(); // root node (index 0)
    root.value     = 0;
    root.out_link  = 0;
    root.fail_link = 0;

    std::uint32_t last_state = 1;

    // Insert patterns into trie
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        auto const &word    = patterns[i];
        state_type  current = 0;
        for (char32_t const c : word) {
            auto next = find_child(current, c);
            if (next == 0) {
                // create new node
                auto &last         = trie.emplace_back();
                last.value         = c;
                last.out_link      = 0;
                last.fail_link     = 0;
                next               = last_state;
                last.children_mask = add_child(current, c, next);
                ++last_state;
            }
            current = next;
        }

        // pattern index too large for bitmask; you may want to change representation.
        if (i >= MAX_PATTERNS) {
            throw std::runtime_error("Too many patterns added.");
        }

        // set output bit for pattern i
        trie[current].out_link |= 1U << i;
    }

    // Build failure links using BFS
    std::queue<state_type> q;
    // initialize root's children: their fail is root (0)
    for (auto const [_, child_index] : trie[0].children) {
        trie[child_index].fail_link = 0;
        q.push(child_index);
    }

    while (!q.empty()) {
        auto const cstate = q.front();
        q.pop();

        // iterate over each child of cstate
        for (auto const &[code, child_index] : trie[cstate].children) {
            // compute failure for child_index
            auto fail = trie[cstate].fail_link;
            // walk fail links until we find a node that has `ch` as child or reach root
            while (fail != 0 && find_child(fail, code) == 0) {
                fail = trie[fail].fail_link;
            }
            auto const fchild = find_child(fail, code);
            if (fchild != 0) {
                trie[child_index].fail_link = fchild;
            } else {
                trie[child_index].fail_link = 0;
            }

            // merge output links
            trie[child_index].out_link |= trie[trie[child_index].fail_link].out_link;

            q.push(child_index);
        }
    }

    return last_state;
}

void basic_search_engine::add_pattern(std::string_view pattern) {
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

    // Handle custom modifiers and actions
    replace_modifiers_and_actions(encoded_pattern);

    patterns.emplace_back(std::move(encoded_pattern));

    // Rebuild machine (can be optimized to incremental insertion if needed)
    build_machine();
}

foresight::aho_state basic_search_engine::process(char32_t const  code_point,
                                                  aho_state const last_state) const noexcept {
    assert(!trie.empty());
    auto state = last_state.index();

    // follow transitions; if not present, follow failure links until root
    auto next = find_child(state, code_point);
    while (next == 0 && state != 0) {
        state = trie[state].fail_link;
        next  = find_child(state, code_point);
    }
    return last_state.next_generation(next);
}

void basic_search_engine::matches(std::uint32_t const                             state,
                                  std::function<void(std::u32string_view)> const &callback) const {
    assert(patterns.size() < MAX_PATTERNS);
    if (state >= trie.size()) [[unlikely]] {
        return;
    }
    auto const mask = trie[state].out_link;
    for (std::size_t j = 0; j < patterns.size(); ++j) {
        if ((mask & (1U << j)) != 0u) {
            callback(patterns[j]);
        }
    }
}

bool basic_search_engine::matches(std::uint32_t const state, std::uint16_t const trigger_id) const noexcept {
    assert(state < trie.size());
    assert(trigger_id < MAX_PATTERNS);
    auto const mask = trie[state].out_link;
    return (mask & (1U << trigger_id)) != 0u;
}

void basic_search_engine::operator()(start_tag) {
    // create empty machine (root-only)
    build_machine();
}

// NOLINTEND(*-pro-bounds-constant-array-index)
