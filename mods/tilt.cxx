// Created by moisrex on 9/12/26.

module;
#include <algorithm>
#include <chrono>
#include <cmath>
#include <linux/input-event-codes.h>
module fs8.mods;

using fs8::basic_tilt_state;
using fs8::context_action;
using fs8::evdev;
using fs8::event_type;
using fs8::tilt_default_tilt_range;

namespace fs8::tilt_detail {

    tilt_axis classify_abs(event_type const& event) noexcept {
        switch (event.hash()) {
            case hashed(EV_ABS, ABS_X): return {.valid = true, .is_x = true, .is_abs = true};
            case hashed(EV_ABS, ABS_Y): return {.valid = true, .is_abs = true};
            case hashed(EV_KEY, BTN_TOOL_PEN):
            case hashed(EV_KEY, BTN_TOOL_RUBBER):
            case hashed(EV_KEY, BTN_TOOL_BRUSH):
            case hashed(EV_KEY, BTN_TOOL_PENCIL):
            case hashed(EV_KEY, BTN_TOOL_AIRBRUSH):
            case hashed(EV_KEY, BTN_TOOL_FINGER):
            case hashed(EV_KEY, BTN_TOOL_MOUSE):
            case hashed(EV_KEY, BTN_TOOL_LENS): return {.reset = true};
            default: return {};
        }
    }

    tilt_axis classify_rel(event_type const& event) noexcept {
        switch (event.hash()) {
            case hashed(EV_REL, REL_X): return {.valid = true, .is_x = true};
            case hashed(EV_REL, REL_Y): return {.valid = true};
            case hashed(EV_KEY, BTN_TOOL_PEN):
            case hashed(EV_KEY, BTN_TOOL_RUBBER):
            case hashed(EV_KEY, BTN_TOOL_BRUSH):
            case hashed(EV_KEY, BTN_TOOL_PENCIL):
            case hashed(EV_KEY, BTN_TOOL_AIRBRUSH):
            case hashed(EV_KEY, BTN_TOOL_FINGER):
            case hashed(EV_KEY, BTN_TOOL_MOUSE):
            case hashed(EV_KEY, BTN_TOOL_LENS): return {.reset = true};
            default: return {};
        }
    }

    void isotropic_mapping(basic_tilt_state const& state, float& t_x, float& t_y) noexcept {
        float const amount = state.normalized_magnitude();
        t_x                = amount;
        t_y                = amount;
    }

    void per_axis_mapping(basic_tilt_state const& state, float& t_x, float& t_y) noexcept {
        t_x = std::abs(state.norm_x());
        t_y = std::abs(state.norm_y());
    }

    namespace {
        [[nodiscard]] float push_amount(float const norm, tilt_push_options const& options) noexcept {
            if (std::abs(norm) <= options.dead_zone) {
                return 0.0F;
            }
            return options.gain * norm;
        }
    } // namespace

    context_action
    apply_speed(event_type& event, tilt_axis const axis, tilt_scale_state& scale, float const x_factor, float const y_factor) noexcept {
        using enum context_action;
        float&      eps    = axis.is_x ? scale.x_eps : scale.y_eps;
        float const factor = axis.is_x ? x_factor : y_factor;
        if (axis.is_abs) {
            event_type::value_type& last  = axis.is_x ? scale.x_last : scale.y_last;
            event_type::value_type& out   = axis.is_x ? scale.x_out : scale.y_out;
            bool&                   init  = axis.is_x ? scale.x_init : scale.y_init;
            auto const              value = event.value();
            if (!init) {
                last = value;
                out  = value;
                init = true;
                return next;
            }
            auto const delta    = static_cast<float>(value - last);
            last                = value;
            float const scaled  = (delta * factor) + eps;
            auto const  pixels  = static_cast<event_type::value_type>(scaled);
            eps                 = scaled - static_cast<float>(pixels);
            out                += pixels;
            event.value(out);
            return next;
        }
        float const scaled = (static_cast<float>(event.value()) * factor) + eps;
        auto const  out    = static_cast<event_type::value_type>(scaled);
        eps                = scaled - static_cast<float>(out);
        event.value(out);
        return next;
    }

    context_action apply_freeze(event_type& event, tilt_axis const axis, tilt_scale_state& scale, bool const frozen) noexcept {
        using enum context_action;
        if (axis.is_abs) {
            event_type::value_type& last  = axis.is_x ? scale.x_last : scale.y_last;
            event_type::value_type& out   = axis.is_x ? scale.x_out : scale.y_out;
            bool&                   init  = axis.is_x ? scale.x_init : scale.y_init;
            auto const              value = event.value();
            if (!init) {
                last = value;
                out  = value;
                init = true;
                return next;
            }
            last = value;
            if (frozen) {
                event.value(out); // hold: abs2rel sees a zero delta
            } else {
                out = value;
            }
            return next;
        }
        if (frozen) {
            event.value(0);
        }
        return next;
    }

    context_action apply_push(
      event_type&              event,
      tilt_axis const          axis,
      tilt_scale_state&        scale,
      float const              x_norm,
      float const              y_norm,
      tilt_push_options const& options) noexcept {
        using enum context_action;
        float const norm = axis.is_x ? x_norm : y_norm;
        float&      eps  = axis.is_x ? scale.x_eps : scale.y_eps;
        if (axis.is_abs) {
            event_type::value_type& last  = axis.is_x ? scale.x_last : scale.y_last;
            bool&                   init  = axis.is_x ? scale.x_init : scale.y_init;
            auto const              value = event.value();
            if (!init) {
                last = value;
                init = true;
                return next;
            }
            if (value == last) {
                return next; // no movement: don't drift
            }
            last               = value;
            float const scaled = static_cast<float>(value) + push_amount(norm, options) + eps;
            auto const  out    = static_cast<event_type::value_type>(scaled);
            eps                = scaled - static_cast<float>(out);
            event.value(out);
            return next;
        }
        float const scaled = static_cast<float>(event.value()) + push_amount(norm, options) + eps;
        auto const  out    = static_cast<event_type::value_type>(scaled);
        eps                = scaled - static_cast<float>(out);
        event.value(out);
        return next;
    }

} // namespace fs8::tilt_detail

bool basic_tilt_state::seed_range(evdev const& dev) noexcept {
    auto const* x_info = dev.abs_info(ABS_TILT_X);
    auto const* y_info = dev.abs_info(ABS_TILT_Y);
    if (x_info == nullptr || y_info == nullptr) {
        return false;
    }
    range_x_ = static_cast<float>(std::max(std::abs(x_info->minimum), std::abs(x_info->maximum)));
    range_y_ = static_cast<float>(std::max(std::abs(y_info->minimum), std::abs(y_info->maximum)));
    if (range_x_ <= 0.0F) {
        range_x_ = tilt_default_tilt_range;
    }
    if (range_y_ <= 0.0F) {
        range_y_ = tilt_default_tilt_range;
    }
    return true;
}

void basic_tilt_state::update(value_type const value, bool const is_x, std::chrono::microseconds const now) noexcept {
    if (is_x) {
        tilt_x_ = value;
    } else {
        tilt_y_ = value;
    }

    float const raw_x = range_x_ > 0.0F ? static_cast<float>(tilt_x_) / range_x_ : 0.0F;
    float const raw_y = range_y_ > 0.0F ? static_cast<float>(tilt_y_) / range_y_ : 0.0F;

    float const dx = range_x_ > 0.0F ? static_cast<float>(tilt_x_ - last_x_) / range_x_ : 0.0F;
    float const dy = range_y_ > 0.0F ? static_cast<float>(tilt_y_ - last_y_) / range_y_ : 0.0F;

    last_x_ = tilt_x_;
    last_y_ = tilt_y_;

    if (pending_base_capture_) {
        // Capture the entry-frame reading per axis; the other axis keeps the
        // snapshot taken in begin_proximity (already its current value).
        if (is_x) {
            base_x_ = raw_x;
        } else {
            base_y_ = raw_y;
        }
    } else if (options.recenter_time > 0.0F && last_update_time_ != std::chrono::microseconds::zero()) {
        float const dt = std::chrono::duration<float>{now - last_update_time_}.count();
        if (dt > 0.0F) {
            float const alpha  = std::clamp(1.0F - std::exp(-dt / options.recenter_time), 0.0F, 1.0F);
            base_x_           += alpha * (raw_x - base_x_);
            base_y_           += alpha * (raw_y - base_y_);
        }
    }
    last_update_time_ = now;

    norm_x_    = std::clamp(raw_x - base_x_, -1.0F, 1.0F);
    norm_y_    = std::clamp(raw_y - base_y_, -1.0F, 1.0F);
    magnitude_ = std::clamp(std::sqrt((norm_x_ * norm_x_) + (norm_y_ * norm_y_)), 0.0F, 1.0F);
    change_    = std::clamp(std::sqrt((dx * dx) + (dy * dy)), 0.0F, 1.0F);

    ++version_;
}

void basic_tilt_state::begin_proximity(std::chrono::microseconds const now) noexcept {
    pending_base_capture_ = true;
    base_x_               = range_x_ > 0.0F ? static_cast<float>(tilt_x_) / range_x_ : 0.0F;
    base_y_               = range_y_ > 0.0F ? static_cast<float>(tilt_y_) / range_y_ : 0.0F;
    last_x_               = tilt_x_;
    last_y_               = tilt_y_;
    norm_x_               = 0.0F;
    norm_y_               = 0.0F;
    magnitude_            = 0.0F;
    change_               = 0.0F;
    last_update_time_     = now;
    ++version_;
}

void basic_tilt_state::reset() noexcept {
    tilt_x_               = 0;
    tilt_y_               = 0;
    last_x_               = 0;
    last_y_               = 0;
    norm_x_               = 0.0F;
    norm_y_               = 0.0F;
    magnitude_            = 0.0F;
    change_               = 0.0F;
    base_x_               = 0.0F;
    base_y_               = 0.0F;
    pending_base_capture_ = false;
    last_update_time_     = std::chrono::microseconds::zero();
    ++version_;
}

context_action basic_tilt_state::operator()(event_type const& event) noexcept {
    using enum context_action;

    switch (event.hash()) {
        case hashed(EV_ABS, ABS_TILT_X): update(event.value(), true, event.micro_time()); return next;
        case hashed(EV_ABS, ABS_TILT_Y): update(event.value(), false, event.micro_time()); return next;

        case hashed(EV_SYN, SYN_REPORT): pending_base_capture_ = false; return next;

        case hashed(EV_KEY, BTN_TOOL_PEN):
        case hashed(EV_KEY, BTN_TOOL_RUBBER):
        case hashed(EV_KEY, BTN_TOOL_BRUSH):
        case hashed(EV_KEY, BTN_TOOL_PENCIL):
        case hashed(EV_KEY, BTN_TOOL_AIRBRUSH):
        case hashed(EV_KEY, BTN_TOOL_FINGER):
        case hashed(EV_KEY, BTN_TOOL_MOUSE):
        case hashed(EV_KEY, BTN_TOOL_LENS):
            if (event.value() != 0) {
                begin_proximity(event.micro_time());
            } else {
                reset();
            }
            return next;

        default: return next;
    }
}
