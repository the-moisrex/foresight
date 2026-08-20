// Created by moisrex on 8/18/26.

module;
#include <chrono>
#include <cstdint>
module fs8.mods;
import fs8.event;
import :typed;
import fs8.pimpl;

using fs8::basic_timed_typed;

template <>
struct fs8::pimpl_idiom<basic_timed_typed>::impl {
    std::uint16_t             trigger_id = basic_timed_typed::invalid_trigger_id; // pattern id in the search engine
    fs8::aho_state            aho_search_state{};                                 // the state of where we are in search engine
    std::chrono::microseconds last_time{};                                        // the time of the last relevant key event
};

fs8::context_action fs8::basic_timed_typed::on_start(fs8::basic_search_engine& engine) noexcept try {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->trigger_id = engine.emplace_pattern(pattern);
    return fs8::context_action::next;
} catch (...) {
    // Keep the mod disabled instead of terminating the whole pipeline.
    pimpl->trigger_id = invalid_trigger_id;
    return fs8::context_action::idle;
}

bool fs8::basic_timed_typed::on_search(event_type const& event, basic_search_engine const& engine) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return false;
    }
    return engine.timed_search(event, pimpl->trigger_id, keyboard_state, pimpl->aho_search_state, duration, pimpl->last_time);
}
