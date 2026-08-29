// Created by moisrex on 8/21/26.

module;
#include <linux/input-event-codes.h>
#include <string>
module fs8.mods;

import fs8.event;

namespace fs8 {

    std::string_view to_string(sanitizer_issue const issue) noexcept {
        using enum sanitizer_issue;
        switch (issue) {
            case none: return {"event is clean"};
            case adjacent_syn: return {"duplicate SYN_REPORT (no data since last syn)"};
            case orphan_release: return {"key release without a prior press"};
            case orphan_repeat: return {"key repeat without a prior press"};
            case double_press: return {"key press while already pressed"};
            case late_syn: return {"SYN_REPORT arrived after a long gap with no data"};
            case out_of_resolution: return {"pen ABS value outside device bounds"};
            case big_jump: return {"mouse movement exceeding threshold"};
            case missing_syn_time: return {"data events long after last SYN_REPORT"};
            case missing_syn_count: return {"too many data events without SYN_REPORT"};
            case missing_syn_travel: return {"mouse travel exceeding threshold without SYN_REPORT"};
            case orphan_abs: return {"ABS position event without a preceding tool press"};
            default: break;
        }
        return {"<unknown>"};
    }

    std::string to_string(sanitizer_issue const issue, event_type const& event) noexcept {
        return std::string{to_string(issue)} + ": " + event.type_name() + " " + event.code_name() + " " + std::to_string(event.value());
    }

    context_action basic_log_diagnostics::operator()(event_type const& event) const noexcept {
        log("{} {} {}", event.type_name(), event.code_name(), event.value());
        return context_action::next;
    }

    context_action basic_log_diagnostics::operator()(event_type const& event, sanitizer_issue const reason) const noexcept {
        log("[{}] {}", to_string(reason), to_string(reason, event));
        return context_action::next;
    }

    context_action basic_log_and_drop_action::operator()(event_type const& event) const noexcept {
        log("{} {} {}", event.type_name(), event.code_name(), event.value());
        return context_action::drop_event;
    }

    context_action basic_log_and_drop_action::operator()(event_type const& event, sanitizer_issue const reason) const noexcept {
        log("[{}] {}", to_string(reason), to_string(reason, event));
        return context_action::drop_event;
    }

} // namespace fs8
