// Created by moisrex on 6/29/25.

module;
#include <cassert>
#include <cmath>
#include <linux/input-event-codes.h>
module foresight.mods.quantifier;

using foresight::basic_mice_quantifier;
using foresight::basic_quantifier;

basic_quantifier::value_type basic_quantifier::consume_steps() noexcept {
    auto const step_count  = std::abs(value) / step;
    value                 %= step;
    return step_count;
}

basic_mice_quantifier::basic_mice_quantifier(value_type const inp_step) noexcept : step{inp_step} {
    assert(step > 0);
}

void basic_mice_quantifier::process(event_type const& event) noexcept {
    if (event.type() != EV_REL) {
        return;
    }
    switch (event.code()) {
        case REL_X: x_value += event.value(); break;
        case REL_Y: y_value += event.value(); break;
        default: break;
    }
}

basic_mice_quantifier::value_type basic_mice_quantifier::consume_x() noexcept {
    auto const step_count  = std::abs(x_value) / step;
    x_value               %= step;
    return step_count;
}

basic_mice_quantifier::value_type basic_mice_quantifier::consume_y() noexcept {
    auto const step_count  = std::abs(y_value) / step;
    y_value               %= step;
    return step_count;
}

basic_quantifier::basic_quantifier(value_type const inp_step) noexcept : step{inp_step} {
    assert(step > 0);
}

void basic_quantifier::process(event_type const& event, code_type const btn_code) noexcept {
    if (event.type() != EV_REL || event.code() != btn_code) {
        return;
    }
    value += event.value();
}
