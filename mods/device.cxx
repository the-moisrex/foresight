// Created by moisrex on 8/17/26.

module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
module fs8.mods;
import fs8.devices.evdev;
import fs8.devices.queries;

using fs8::basic_from_query;

void basic_from_query::set(device_query const& inp_query) noexcept {
    query_.set(inp_query);
    for (auto& entry : sources_) {
        if (entry.dev != nullptr) {
            entry.matched = match(*entry.dev);
        }
    }
}

void basic_from_query::operator()(control_event const& tag) noexcept {
    if (tag.code != source_registered.code || tag.payload == nullptr) [[unlikely]] {
        return;
    }
    auto const& info = payload<source_registered>(tag);
    switch (tag.value) {
        case source_registered.value: upsert(identity_of(info.source_id), info.device); return;
        case source_unregistered.value: remove(identity_of(info.source_id)); return;
        default: return; // source_owned: the id is unchanged, nothing to map
    }
}

bool basic_from_query::operator()(event_type const& event) const noexcept {
    auto const id = identity_of(event.source());
    if (id == source_id_none) [[unlikely]] {
        return false;
    }
    for (auto const& entry : sources_) {
        if (entry.id == id) {
            return entry.matched;
        }
    }
    return false;
}

/// The answer only depends on (source id, device): match_caps and the udev
/// lookup behind matches_full don't come cheap per event, so they run once
/// per registration.  The device pointer ties the entry to one concrete
/// device, so a source_index reused for another device gets a fresh answer —
/// and unlike a name hash, a hit costs no string hashing on the hot path.
bool basic_from_query::match(evdev const& dev) const noexcept {
    return device_sysname(dev).empty() ? matches(dev, query_.value()) : matches_full(dev, query_.value());
}

void basic_from_query::upsert(std::uint32_t const id, evdev const* const dev) noexcept {
    if (id == source_id_none || dev == nullptr) [[unlikely]] {
        return;
    }
    auto const matched = match(*dev);
    for (auto& entry : sources_) {
        if (entry.id == id) {
            entry.dev     = dev;
            entry.matched = matched;
            return;
        }
    }
    for (auto& entry : sources_) {
        if (entry.id == source_id_none) {
            entry = source_entry{id, dev, matched};
            return;
        }
    }
    sources_[next_ % sources_.size()] = source_entry{id, dev, matched};
    ++next_;
}

void basic_from_query::remove(std::uint32_t const id) noexcept {
    if (id == source_id_none) [[unlikely]] {
        return;
    }
    for (auto& entry : sources_) {
        if (entry.id == id) {
            entry = source_entry{};
            return;
        }
    }
}
