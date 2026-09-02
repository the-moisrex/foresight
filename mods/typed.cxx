// Created by moisrex on 10/28/25.

module;
#include <cassert>
#include <chrono>
#include <climits>
#include <cstdint>
#include <functional>
#include <linux/input-event-codes.h>
#include <queue>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
module fs8.mods;
import fs8.lib.xkb.how2type;
import fs8.event;

using fs8::basic_search_engine;
using fs8::basic_typed;

namespace {

    std::uint32_t calc_children_mask(auto const &node) noexcept {
        std::uint32_t mask = node.children_mask;
        for (auto const &child : node.children) {
            mask |= child.first;
        }
        return mask;
    }
} // namespace

template <>
struct fs8::pimpl_idiom<basic_search_engine>::impl {
    struct node_type {
        char32_t      value     = 0; // the incoming code point for this node (root = 0)
        std::uint32_t out_link  = 0; // output bitmask (pattern IDs)
        std::uint32_t fail_link = 0; // failure link (state index)

        // children: pair<codepoint, state_index>. kept sorted by codepoint for binary search.
        std::vector<std::pair<char32_t, std::uint32_t>> children;

        // A mask for all children keys for faster failures
        std::uint32_t children_mask = 0U;
    };

    /// UTF-32-encoded patterns (some code points are special code points)
    /// Trigger ID is the index that points to this patterns
    /// todo: use inplace_vector<std::u32string, MAX_PATTERNS>
    std::vector<std::u32string> patterns;
    /// The modifier mode of each pattern (parallel to `patterns`)
    std::vector<modifier_mode> pattern_modes;
    std::vector<node_type>     trie;
};

template <>
struct fs8::pimpl_idiom<basic_typed>::impl {
    std::uint16_t trigger_id = basic_typed::invalid_trigger_id; // pattern id in the search engine
    aho_state     aho_search_state{};                           // the state of where we are in search engine
};

// NOLINTBEGIN(*-pro-bounds-constant-array-index)
basic_search_engine::state_type basic_search_engine::find_child(state_type const state, char32_t const code) const noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return 0;
    }
    auto const &node     = pimpl->trie[state];
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
    if (pimpl.get() == nullptr) [[unlikely]] {
        return 0;
    }
    if ((pimpl->trie[state].children_mask & code) == 0) [[likely]] {
        // User most likely won't be typing the shortcuts all the time, so we put it in the slow path
        return 0; // index to root
    }
    return find_child(state, code);
}

std::uint32_t basic_search_engine::add_child(state_type const state, char32_t code, state_type child_index) {
    auto      &node     = pimpl->trie[state];
    auto      &children = pimpl->trie[state].children;
    auto const it       = std::lower_bound(children.begin(), children.end(), code, [](auto const &a, char32_t value) {
        return a.first < value;
    });
    children.emplace(it, code, child_index); // insert sorted
    node.children_mask |= code;
    return calc_children_mask(node);
}

std::uint32_t basic_search_engine::build_machine() {
    pimpl->trie.clear();
    {
        auto &root     = pimpl->trie.emplace_back(); // root node (index 0)
        root.value     = 0;
        root.out_link  = 0;
        root.fail_link = 0;
    }

    std::uint32_t last_state = 1;
    std::uint32_t index      = 0;

    // Insert patterns into trie
    for (auto const &pattern : pimpl->patterns) {
        state_type current = 0;
        for (char32_t const c : pattern) {
            auto next = find_child(current, c);
            if (next == 0) {
                // create new node
                auto &last         = pimpl->trie.emplace_back();
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
        pimpl->trie[current].out_link |= 1U << index;
        ++index;
    }


    // Build failure links using BFS
    std::queue<state_type> q;

    // initialize root's children: their fail is root (0)
    for (auto const [_, child_index] : pimpl->trie[0].children) {
        pimpl->trie[child_index].fail_link = 0;
        q.push(child_index);
    }
    pimpl->trie[0].children_mask = calc_children_mask(pimpl->trie[0]);

    while (!q.empty()) {
        auto const cstate = q.front();
        q.pop();

        // iterate over each child of cstate
        for (auto const &[code, child_index] : pimpl->trie[cstate].children) {
            // compute failure for child_index
            auto fail = pimpl->trie[cstate].fail_link;
            // walk fail links until we find a node that has `ch` as child or reach root
            while (fail != 0 && find_child(fail, code) == 0) {
                fail = pimpl->trie[fail].fail_link;
            }
            auto const fchild = find_child(fail, code);
            if (fchild != 0) {
                pimpl->trie[child_index].fail_link = fchild;
            } else {
                pimpl->trie[child_index].fail_link = 0;
            }

            // merge output links
            pimpl->trie[child_index].out_link |= pimpl->trie[pimpl->trie[child_index].fail_link].out_link;

            q.push(child_index);
        }
    }

    return last_state;
}

std::uint16_t basic_search_engine::emplace_pattern(std::string_view const pattern) {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    auto const    mode      = modifier_mode_of(pattern);
    auto          e_pattern = encoded_modifiers(pattern);
    auto const    it        = std::ranges::find(pimpl->patterns, e_pattern);
    std::uint16_t index     = 0;
    if (it == pimpl->patterns.end()) {
        // insert it if we didn't find it
        pimpl->patterns.emplace_back(std::move(e_pattern));
        pimpl->pattern_modes.push_back(mode);
        index = static_cast<std::uint16_t>(pimpl->patterns.size() - 1);

        // Rebuild machine (can be optimized to incremental insertion if needed)
        build_machine();
    } else {
        index = static_cast<std::uint16_t>(std::distance(pimpl->patterns.begin(), it));
    }
    return index;
}

fs8::aho_state basic_search_engine::process(char32_t const code_point, aho_state const last_state) const noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return last_state.next_generation(0);
    }
    assert(!pimpl->trie.empty());
    auto state = last_state.index();

    // follow transitions; if not present, follow failure links until root
    auto next = quick_find_child(state, code_point);
    while (next == 0 && state != 0) {
        state = pimpl->trie[state].fail_link;
        next  = quick_find_child(state, code_point);
    }
    return last_state.next_generation(next);
}

void basic_search_engine::matches(std::uint32_t const state, std::function_ref<void(std::u32string_view)> callback) const {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }
    assert(pimpl->patterns.size() < MAX_PATTERNS);
    if (state >= pimpl->trie.size()) [[unlikely]] {
        return;
    }
    auto const mask = pimpl->trie[state].out_link;
    for (std::size_t j = 0; j < pimpl->patterns.size(); ++j) {
        if ((mask & (1U << j)) != 0u) {
            callback(pimpl->patterns[j]);
        }
    }
}

bool basic_search_engine::matches(std::uint32_t const state, std::uint16_t const trigger_id) const noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return false;
    }
    assert(state < pimpl->trie.size());
    assert(trigger_id < MAX_PATTERNS);
    auto const mask = pimpl->trie[state].out_link;
    return (mask >> trigger_id & 0b1U) != 0u;
}

fs8::context_action basic_search_engine::operator()(special_event const &tag) noexcept try {
    if (tag.code != start.code) {
        return fs8::context_action::drop_event;
    }
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->trie.clear(); // clear the trie in case of a restart

    // create empty machine (root-only)
    build_machine();
    return fs8::context_action::next;
} catch (...) {
    // clear the trie in case of a failed build; keep the pipeline running in a degraded state
    pimpl->trie.clear();
    return fs8::context_action::recovery;
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

    if (trigger_id >= pimpl->pattern_modes.size()) [[unlikely]] {
        return false;
    }
    auto const mode       = pimpl->pattern_modes[trigger_id];
    bool const is_up_mode = mode == modifier_mode::keyup || mode == modifier_mode::ordered_keyup;
    // keydown patterns only track presses, keyup patterns only track releases:
    if (is_up_mode ? event.value() != 0 : event.value() != 1) {
        return false;
    }

    state = process(code, state);
    return matches(state.index(), trigger_id);
}

bool basic_search_engine::timed_search(
  event_type const               &event,
  std::uint16_t const             trigger_id,
  xkb::basic_state const         &keyboard_state,
  aho_state                      &state,
  std::chrono::microseconds const max_gap,
  std::chrono::microseconds      &last_time) const noexcept {
    if (event.type() != EV_KEY) {
        return false;
    }
    auto const code = unicode_encoded_event(keyboard_state, static_cast<key_event>(event));

    if (trigger_id >= pimpl->pattern_modes.size()) [[unlikely]] {
        return false;
    }
    auto const mode       = pimpl->pattern_modes[trigger_id];
    bool const is_up_mode = mode == modifier_mode::keyup || mode == modifier_mode::ordered_keyup;
    // keydown patterns only track presses, keyup patterns only track releases:
    if (is_up_mode ? event.value() != 0 : event.value() != 1) {
        return false;
    }

    auto const now = event.micro_time();
    // If the user paused too long between two characters of a pattern, forget
    // the partial match we were building up.
    if (state.index() != 0 && now - last_time > max_gap) {
        state = aho_state{};
    }
    state     = process(code, state);
    last_time = now;
    return matches(state.index(), trigger_id);
}

fs8::context_action basic_typed::on_start(basic_search_engine &engine) noexcept try {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->trigger_id = engine.emplace_pattern(pattern);
    return fs8::context_action::next;
} catch (...) {
    // Keep the mod disabled instead of terminating the whole pipeline.
    pimpl->trigger_id = invalid_trigger_id;
    return fs8::context_action::recovery;
}

bool basic_typed::on_search(event_type const &event, basic_search_engine const &engine) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return false;
    }
    return engine.search(event, pimpl->trigger_id, keyboard_state, pimpl->aho_search_state);
}

// NOLINTEND(*-pro-bounds-constant-array-index)
