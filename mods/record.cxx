// Created by moisrex on 8/17/26.

module;
#include <algorithm>
#include <linux/input-event-codes.h>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
module fs8.mods;
import fs8.event;
import fs8.context;
import fs8.pimpl;

using fs8::basic_record;
using fs8::context_action;
using fs8::event_type;
using fs8::user_event;

template <>
struct fs8::pimpl_idiom<basic_record>::impl {
    std::vector<event_type> events;
};

context_action basic_record::record_event(event_type const& event) noexcept try {
    if (sink != nullptr) [[unlikely]] {
        sink->push_back(event);
    } else {
        if (pimpl.get() == nullptr) {
            init_impl();
        }
        pimpl->events.push_back(event);
    }
    return context_action::next;
} catch (...) {
    log("Allocation failure: silently drop the event rather than terminate.");
    // Allocation failure: silently drop the event rather than terminate.
    return context_action::next;
}

std::span<event_type const> basic_record::events() const noexcept {
    if (sink != nullptr) [[unlikely]] {
        return {*sink};
    }
    if (!static_cast<bool>(pimpl)) [[unlikely]] {
        return {};
    }
    return pimpl->events;
}

std::size_t basic_record::size() const noexcept {
    return events().size();
}

bool basic_record::empty() const noexcept {
    return events().empty();
}

void basic_record::clear() noexcept {
    if (sink != nullptr) {
        sink->clear();
    } else if (static_cast<bool>(pimpl)) {
        pimpl->events.clear();
    }
}

event_type const& basic_record::operator[](std::size_t const index) const noexcept {
    return events()[index];
}

event_type const& basic_record::at(std::size_t const index) const {
    auto const evs = events();
    if (index >= evs.size()) [[unlikely]] {
        throw std::out_of_range{"basic_record::at"};
    }
    return evs[index];
}

event_type const& basic_record::front() const noexcept {
    return events().front();
}

event_type const& basic_record::back() const noexcept {
    return events().back();
}

std::span<event_type const>::const_iterator basic_record::begin() const noexcept {
    return events().begin();
}

std::span<event_type const>::const_iterator basic_record::end() const noexcept {
    return events().end();
}

std::vector<user_event> basic_record::as_user_events() const noexcept {
    auto const              evs = events();
    std::vector<user_event> out;
    try {
        out.reserve(evs.size());
        for (auto const& event : evs) {
            out.push_back(static_cast<user_event>(event));
        }
    } catch (...) {
        // Return whatever we've collected so far.
    }
    return out;
}

std::size_t basic_record::count(event_type::type_type const type) const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(events(), [type](event_type const& event) noexcept {
        return event.type() == type;
    }));
}

std::size_t basic_record::count(event_type::type_type const type, event_type::code_type const code) const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(events(), [type, code](event_type const& event) noexcept {
        return event.is(type, code);
    }));
}

std::vector<event_type> basic_record::keys() const noexcept {
    std::vector<event_type> out;
    try {
        for (auto const& event : events()) {
            if (event.type() == EV_KEY) {
                out.push_back(event);
            }
        }
    } catch (...) {
        log("Allocation failure during vector creation.");
        // Return whatever we've collected so far.
    }
    return out;
}

std::vector<event_type> basic_record::without_syn() const noexcept {
    std::vector<event_type> out;
    try {
        for (auto const& event : events()) {
            if (event.type() != EV_SYN) {
                out.push_back(event);
            }
        }
    } catch (...) {
        log("Allocation failure during event creation.");
        // Return whatever we've collected so far.
    }
    return out;
}
