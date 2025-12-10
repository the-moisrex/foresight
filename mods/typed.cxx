// Created by moisrex on 10/28/25.

module;
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <linux/input-event-codes.h>
#include <queue>
#include <string>
module foresight.mods.typed;
import foresight.lib.xkb.how2type;
import foresight.main.log;
import foresight.utils.hash;
import foresight.mods.event;
import foresight.lib.mod_parser;

using fs8::basic_search_engine;

namespace {

    std::uint32_t calc_children_mask(auto const &node) noexcept {
        std::uint32_t mask = node.children_mask;
        for (auto const &child : node.children) {
            mask |= child.first;
        }
        return mask;
    }
} // namespace

// NOLINTBEGIN(*-pro-bounds-constant-array-index)
basic_search_engine::state_type basic_search_engine::find_child(state_type const state, char32_t const code) const noexcept {
    auto const &node     = trie[state];
    auto const &children = node.children;

    // binary search since children is kept sorted by codepoint
    auto const it = std::lower_bound(children.begin(), children.end(), code, [](auto const &a, char32_t value) {
        return a.first < value;
    });
    if (it != children.end() && it->first == code) {
        return it->second;
    }
    return 0; // index to root
}

basic_search_engine::state_type basic_search_engine::quick_find_child(state_type const state, char32_t const code) const noexcept {
    if ((trie[state].children_mask & code) == 0) [[likely]] {
        // User most likely won't be typing the shortcuts all the time, so we put it in the slow path
        return 0; // index to root
    }
    return find_child(state, code);
}

std::uint32_t basic_search_engine::add_child(state_type const state, char32_t code, state_type child_index) {
    auto      &node     = trie[state];
    auto      &children = trie[state].children;
    auto const it       = std::lower_bound(children.begin(), children.end(), code, [](auto const &a, char32_t value) {
        return a.first < value;
    });
    children.emplace(it, code, child_index); // insert sorted
    node.children_mask |= code;
    return calc_children_mask(node);
}

std::uint32_t basic_search_engine::build_machine() {
    trie.clear();
    {
        auto &root     = trie.emplace_back(); // root node (index 0)
        root.value     = 0;
        root.out_link  = 0;
        root.fail_link = 0;
    }

    std::uint32_t last_state = 1;
    std::uint32_t index      = 0;

    // Insert patterns into trie
    for (auto const &pattern : patterns) {
        state_type current = 0;
        for (char32_t const c : pattern) {
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
        if (index >= MAX_PATTERNS) {
            throw std::runtime_error("Too many patterns added.");
        }

        // set output bit for pattern i
        trie[current].out_link |= 1U << index;
        ++index;
    }


    // Build failure links using BFS
    std::queue<state_type> q;

    // initialize root's children: their fail is root (0)
    for (auto const [_, child_index] : trie[0].children) {
        trie[child_index].fail_link = 0;
        q.push(child_index);
    }
    trie[0].children_mask = calc_children_mask(trie[0]);

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

std::uint16_t basic_search_engine::add_pattern(std::string_view const pattern) {
    patterns.emplace_back(encoded_modifiers(pattern));

    // Rebuild machine (can be optimized to incremental insertion if needed)
    build_machine();
    return static_cast<std::uint16_t>(patterns.size() - 1);
}

fs8::aho_state basic_search_engine::process(char32_t const code_point, aho_state const last_state) const noexcept {
    assert(!trie.empty());
    auto state = last_state.index();

    // follow transitions; if not present, follow failure links until root
    auto next = quick_find_child(state, code_point);
    while (next == 0 && state != 0) {
        state = trie[state].fail_link;
        next  = quick_find_child(state, code_point);
    }
    return last_state.next_generation(next);
}

void basic_search_engine::matches(std::uint32_t const state, std::function<void(std::u32string_view)> const &callback) const {
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
    return (mask >> trigger_id & 0b1U) != 0u;
}

void basic_search_engine::operator()(start_tag) {
    // create empty machine (root-only)
    build_machine();
}

bool basic_search_engine::search(
  event_type const       &event,
  std::uint16_t const     trigger_id,
  xkb::basic_state const &keyboard_state,
  aho_state              &state) const noexcept {
    if (event.type() != EV_KEY) {
        return false;
    }
    auto const code = unicode_encoded_event(keyboard_state, static_cast<key_event>(event));
    log("Code: {:x} {} {} {}", static_cast<std::uint32_t>(code), event.type_name(), event.code_name(), event.value());
    if (event.value() != 1) {
        return false; // skip processing key-ups, but we do need to process key-ups for modifier states
    }
    state = process(code, state);
    return matches(state.index(), trigger_id);
}

// NOLINTEND(*-pro-bounds-constant-array-index)
