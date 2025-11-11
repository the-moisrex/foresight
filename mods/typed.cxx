// Created by moisrex on 10/28/25.

module;
#include <cassert>
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

using foresight::basic_typed;
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
                next               = static_cast<int>(last_state);
                last.children_mask = add_child(current, c, next);
                ++last_state;
            }
            current = next;
        }
        // set output bit for pattern i
        if (i < (sizeof(std::uint32_t) * 8)) {
            trie[current].out_link |= (1u << static_cast<unsigned>(i));
        } else {
            // pattern index too large for bitmask; you may want to change representation.
            // For now we silently ignore bits beyond mask (alternatively log or handle).
        }
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
    patterns.emplace_back(std::move(encoded_pattern));

    // Rebuild machine (can be optimized to incremental insertion if needed)
    build_machine();
}

foresight::aho_state basic_search_engine::process(char32_t const  code_point,
                                                  aho_state const last_state) const noexcept {
    assert(!trie.empty());
    auto state = static_cast<int>(last_state.index());

    // follow transitions; if not present, follow failure links until root
    auto next = find_child(state, code_point);
    while (next == 0 && state != 0) {
        state = static_cast<int>(trie[state].fail_link);
        next  = find_child(state, code_point);
    }
    return last_state.next_generation(next);
}

void basic_search_engine::matches(std::uint32_t const                             state,
                                  std::function<void(std::u32string_view)> const &callback) const {
    if (state >= trie.size()) {
        return;
    }
    auto const mask = trie[state].out_link;
    for (std::size_t j = 0; j < patterns.size(); ++j) {
        if (j >= (sizeof(std::uint32_t) * 8)) {
            break; // mask only holds lower bits
        }
        if ((mask & (1U << static_cast<unsigned>(j))) != 0u) {
            callback(patterns[j]);
        }
    }
}

void basic_search_engine::operator()(start_tag) {
    // create empty machine (root-only)
    build_machine();
}

// NOLINTEND(*-pro-bounds-constant-array-index)
