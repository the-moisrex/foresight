// Created by moisrex on 8/21/26.

module;
#include <linux/input-event-codes.h>
module fs8.mods;

import fs8.event;

namespace fs8 {

    context_action basic_log_diagnostics::operator()(event_type const& event) const noexcept {
        log("{} {} {}", event.type_name(), event.code_name(), event.value());
        return context_action::next;
    }

    context_action basic_log_and_drop_action::operator()(event_type const& event) const noexcept {
        log("{} {} {}", event.type_name(), event.code_name(), event.value());
        return context_action::drop_event;
    }

} // namespace fs8
