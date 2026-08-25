// Created by moisrex on 8/17/25.

module;
#include <algorithm>
#include <chrono>
#include <cmath>
#include <linux/input-event-codes.h>
module fs8.mods;

using fs8::basic_momentum_base;
using fs8::context_action;
using fs8::event_type;
using fs8::fsecs;
using fs8::momentum_calculator;
using fs8::msecs;
using fs8::velocity_tracker;

namespace {
    /// Decay factor for momentum events: each successive event is scaled by
    /// `decay_rate^i`.  A value close to 1.0 gives a long tail.
    constexpr float decay_rate = 0.93f;
} // namespace

// ── velocity_tracker ───────────────────────────────────────────────────────

void velocity_tracker::process_event(float const value, msecs const timestamp) noexcept {
    accumulated += value;

    if (last_timestamp.count() == 0) {
        last_timestamp = timestamp;
        return;
    }

    auto const  dt_duration = timestamp - last_timestamp;
    auto const  dt_us       = dt_duration.count();
    float const dt          = static_cast<float>(dt_us) * 1.0e-6f;

    if (dt < 1.0e-6f) {
        return;
    }

    float const     v_instant = value / dt;
    constexpr float tau       = 0.1f;
    float const     alpha     = 1.0f - std::exp(-dt / tau);

    smoothed_velocity = alpha * v_instant + (1.0f - alpha) * smoothed_velocity;
    last_timestamp    = timestamp;
}

float velocity_tracker::velocity() const noexcept {
    return smoothed_velocity;
}

float velocity_tracker::get_recent_delta() const noexcept {
    return accumulated;
}

void velocity_tracker::reset() noexcept {
    accumulated       = 0.0f;
    smoothed_velocity = 0.0f;
    last_timestamp    = msecs::zero();
}

// ── momentum_calculator ────────────────────────────────────────────────────

momentum_calculator::momentum_calculator(
  float const pos, float const delta, float const vel) noexcept
  : delta_(delta), vel_(vel), pos_(pos), target_{pred_dest()} {
    init_curve();
    init_interp();
}

float momentum_calculator::pos_at(fsecs const time) const noexcept {
    float const progress = progress_at(time);
    return linear_only_ ? linear_pos_at(progress) : cubic_pos_at(progress);
}

fsecs momentum_calculator::duration() const noexcept {
    return fsecs{1.0};
}

float momentum_calculator::pred_dest() const noexcept {
    constexpr float factor = 16.7f;
    return pos_ + factor * delta_;
}

void momentum_calculator::set_target(float const target) noexcept {
    target_ = target;
}

float momentum_calculator::linear_pos_at(float const progress) const noexcept {
    float const delta = target_ - pos_;
    return pos_ + progress * delta;
}

float momentum_calculator::cubic_pos_at(float const progress) const noexcept {
    float result = 0.0f;
    for (int i = 0; i < 4; ++i) {
        result += std::pow(progress, static_cast<float>(i)) * coeffs_[i];
    }
    return result;
}

void momentum_calculator::init_interp() noexcept {
    linear_only_ = true;

    if (std::abs(delta_) < 1.0f) {
        return;
    }

    float const to_target      = target_ - pos_;
    float const to_target_dist = std::abs(to_target);
    if (to_target_dist < 0.001f) {
        return;
    }

    float const delta_dir  = (delta_ > 0) ? 1.0f : -1.0f;
    float const target_dir = (to_target > 0) ? 1.0f : -1.0f;
    if (delta_dir != target_dir) {
        return;
    }

    float const side = to_target_dist / (2.0f * std::abs(delta_) / (std::abs(delta_) + to_target_dist) + 1.0f);

    float const ctrl1 = pos_ + side * delta_dir;
    float const ctrl2 = ctrl1 + side * target_dir;

    coeffs_[0] = pos_;
    coeffs_[1] = 3.0f * (ctrl1 - pos_);
    coeffs_[2] = 3.0f * (pos_ - 2.0f * ctrl1 + ctrl2);
    coeffs_[3] = 3.0f * (ctrl1 - ctrl2) - pos_ + target_;

    linear_only_ = false;
}

void momentum_calculator::init_curve() noexcept {
    constexpr int   max_iters = 10;
    constexpr float threshold = 0.001f;
    constexpr float init_mag  = 1.1f;
    constexpr float min_prog  = 0.1f;
    constexpr float max_prog  = 0.5f;
    constexpr float fps       = 60.0f;
    constexpr fsecs anim_dur{1.0};

    float prog = min_prog;

    if (float const to_target = std::abs(target_ - pos_); to_target > 0.001f) {
        float const ratio = std::abs(delta_) / to_target;
        prog              = std::clamp(ratio, min_prog, max_prog);
    }

    float prev_decay = 1.0f;
    curve_mag_       = init_mag;

    for (int i = 0; i < max_iters; ++i) {
        decay_               = curve_mag_ / (curve_mag_ - prog);
        float const exponent = -fps * anim_dur.count();
        curve_mag_           = 1.0f / (1.0f - std::pow(decay_, exponent));

        if (std::abs(decay_ - prev_decay) < threshold) {
            break;
        }
        prev_decay = decay_;
    }
}

float momentum_calculator::progress_at(fsecs const time) const noexcept {
    constexpr float fps       = 60.0f;
    constexpr fsecs anim_dur{1.0};

    float const t        = std::clamp(static_cast<float>(time.count() / anim_dur.count()), 0.0f, 1.0f);
    float const exponent = -fps * static_cast<float>(anim_dur.count() * t);
    return std::min(1.0f, curve_mag_ * (1.0f - std::pow(decay_, exponent)));
}

// ── basic_momentum_base pimpl ──────────────────────────────────────────────

template <>
struct fs8::pimpl_idiom<basic_momentum_base>::impl {
    velocity_tracker      vel_x;
    velocity_tracker      vel_y;
    event_type::type_type track_type   = EV_REL;
    event_type::code_type track_code_x = REL_WHEEL_HI_RES;
    event_type::code_type track_code_y = REL_HWHEEL_HI_RES;
    bool                  is_animating = false;
};

void basic_momentum_base::track(event_type const& event) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }

    auto const  ts   = event.native().time;
    auto const  usec = static_cast<long long>(ts.tv_sec) * 1'000'000LL + static_cast<long long>(ts.tv_usec);
    msecs const timestamp{usec};

    if (event.is(pimpl->track_type, pimpl->track_code_x)) {
        pimpl->vel_x.process_event(static_cast<float>(event.value()), timestamp);
    } else if (event.is(pimpl->track_type, pimpl->track_code_y)) {
        pimpl->vel_y.process_event(static_cast<float>(event.value()), timestamp);
    }
}

bool basic_momentum_base::is_active() const noexcept {
    return pimpl.get() != nullptr;
}

std::pair<float, float> basic_momentum_base::current_velocity() const noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return {0.0f, 0.0f};
    }
    return {pimpl->vel_x.velocity(), pimpl->vel_y.velocity()};
}

float basic_momentum_base::decay_factor(int const i) const noexcept {
    // Convert velocity (pixels/sec) to scroll-wheel units and apply decay.
    // The velocity_tracker stores pixels/sec; scroll-wheel hi-res values
    // are typically in the range [-120..120] per notch.
    constexpr float scale = 0.005f;
    return scale * std::pow(decay_rate, static_cast<float>(i));
}

bool basic_momentum_base::is_animating() const noexcept {
    return pimpl.get() != nullptr && pimpl->is_animating;
}

void basic_momentum_base::set_animating() noexcept {
    if (pimpl.get() != nullptr) {
        pimpl->is_animating = true;
    }
}

void basic_momentum_base::clear_animating() noexcept {
    if (pimpl.get() != nullptr) {
        pimpl->is_animating = false;
    }
}

context_action basic_momentum_base::operator()(start_tag) noexcept {
    if (pimpl.get() == nullptr) {
        init_impl();
    }
    pimpl->vel_x.reset();
    pimpl->vel_y.reset();
    pimpl->is_animating = false;
    return context_action::next;
}

void basic_momentum_base::configure(
  event_type::type_type const t,
  event_type::code_type const cx,
  event_type::code_type const cy) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }
    pimpl->track_type   = t;
    pimpl->track_code_x = cx;
    pimpl->track_code_y = cy;
}
