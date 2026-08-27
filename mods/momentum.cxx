// Created by moisrex on 8/17/25.

module;
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <linux/input-event-codes.h>
#include <span>
module fs8.mods;
import fs8.event;

using fs8::basic_momentum_base;
using fs8::basic_momentum_scroll;
using fs8::basic_scheduler;
using fs8::context_action;
using fs8::event_type;
using fs8::fsecs;
using fs8::momentum_calculator;
using fs8::momentum_config;
using fs8::msecs;
using fs8::syn;
using fs8::velocity_tracker;

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

momentum_calculator::momentum_calculator(float const pos, float const delta, float const vel) noexcept
  : delta_(delta),
    vel_(vel),
    pos_(pos),
    target_{pred_dest()} {
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
    constexpr float fps = 60.0f;
    constexpr fsecs anim_dur{1.0};

    float const t        = std::clamp(static_cast<float>(time.count() / anim_dur.count()), 0.0f, 1.0f);
    float const exponent = -fps * static_cast<float>(anim_dur.count() * t);
    return std::min(1.0f, curve_mag_ * (1.0f - std::pow(decay_, exponent)));
}

// ── basic_momentum_base pimpl ──────────────────────────────────────────────

template <>
struct fs8::pimpl_idiom<basic_momentum_base>::impl {
    velocity_tracker             vel_x;
    velocity_tracker             vel_y;
    basic_scheduler*             scheduler = nullptr;
    basic_scheduler::tick_handle momentum_handle{};
    bool                         is_animating       = false;
    float                        mouse_x            = 0.0f;
    float                        mouse_y            = 0.0f;
    float                        origin_x           = 0.0f;
    float                        origin_y           = 0.0f;
    float                        max_mouse_distance = 0.0f;
};

void basic_momentum_base::track(event_type const& event) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }

    auto const  ts   = event.native().time;
    auto const  usec = static_cast<long long>(ts.tv_sec) * 1'000'000LL + static_cast<long long>(ts.tv_usec);
    msecs const timestamp{usec};

    if (event.is(EV_REL, REL_WHEEL_HI_RES)) {
        pimpl->vel_x.process_event(static_cast<float>(event.value()), timestamp);
    } else if (event.is(EV_REL, REL_HWHEEL_HI_RES)) {
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

void basic_momentum_base::set_max_mouse_distance(float const d) noexcept {
    if (pimpl.get() != nullptr) {
        pimpl->max_mouse_distance = d;
    }
}

void basic_momentum_base::set_mouse_origin() noexcept {
    if (pimpl.get() == nullptr) {
        return;
    }
    pimpl->origin_x = pimpl->mouse_x;
    pimpl->origin_y = pimpl->mouse_y;
}

void basic_momentum_base::update_mouse_distance(event_type const& event) noexcept {
    if (pimpl.get() == nullptr) {
        return;
    }
    if (event.is(EV_REL, REL_X)) {
        pimpl->mouse_x += static_cast<float>(event.value());
    } else if (event.is(EV_REL, REL_Y)) {
        pimpl->mouse_y += static_cast<float>(event.value());
    }
    // When distance tracking is disabled, any mouse move cancels momentum.
    if (pimpl->max_mouse_distance <= 0.0f) {
        cancel_momentum_tick();
        clear_animating();
    }
}

void basic_momentum_base::reset_mouse_distance() noexcept {
    if (pimpl.get() != nullptr) {
        pimpl->origin_x = pimpl->mouse_x;
        pimpl->origin_y = pimpl->mouse_y;
    }
}

float basic_momentum_base::mouse_distance_scale() const noexcept {
    if (pimpl.get() == nullptr || pimpl->max_mouse_distance <= 0.0f) {
        return 1.0f;
    }
    float const dx       = pimpl->mouse_x - pimpl->origin_x;
    float const dy       = pimpl->mouse_y - pimpl->origin_y;
    float const distance = std::max(std::abs(dx), std::abs(dy));
    if (distance >= pimpl->max_mouse_distance) {
        return 0.0f;
    }
    return 1.0f - distance / pimpl->max_mouse_distance;
}

bool basic_momentum_base::has_distance_tracking() const noexcept {
    return pimpl.get() != nullptr && pimpl->max_mouse_distance > 0.0f;
}

void basic_momentum_base::cancel_momentum_tick() noexcept {
    if (pimpl.get() != nullptr && pimpl->scheduler != nullptr) {
        pimpl->scheduler->cancel(pimpl->momentum_handle);
    }
}

context_action basic_momentum_base::operator()(start_tag) noexcept {
    if (pimpl.get() == nullptr) {
        init_impl();
    }
    pimpl->vel_x.reset();
    pimpl->vel_y.reset();
    pimpl->is_animating = false;
    pimpl->mouse_x      = 0.0f;
    pimpl->mouse_y      = 0.0f;
    pimpl->origin_x     = 0.0f;
    pimpl->origin_y     = 0.0f;
    return context_action::next;
}

// ── momentum tick (scroll-specific) ────────────────────────────────────────

namespace {
    struct momentum_tick_state {
        float vel_x          = 0.0f;
        float vel_y          = 0.0f;
        int   step           = 0;
        float distance_scale = 1.0f;
        float acc_x          = 0.0f;
        float acc_y          = 0.0f;
    };

    struct momentum_context {
        basic_momentum_base*   momentum_base = nullptr;
        momentum_config const* config        = nullptr;
        momentum_tick_state    tick_state{};
        /// Buffer for events produced by the tick callback.
        std::array<event_type, 8> event_buffer{};
    };

    basic_scheduler::tick_result momentum_tick(void* const data) noexcept {
        using enum context_action;

        auto& ctx   = *static_cast<momentum_context*>(data);
        auto& state = ctx.tick_state;

        if (state.step >= ctx.config->momentum_frames) {
            return {};
        }

        // Recompute distance scale from live mouse distance tracking.
        if (ctx.momentum_base && ctx.momentum_base->has_distance_tracking()) {
            state.distance_scale = ctx.momentum_base->mouse_distance_scale();
        }

        // Compute per-frame delta (with decay and distance scaling).
        float const factor = ctx.config->initial_scale * std::pow(ctx.config->decay_rate, static_cast<float>(state.step));
        auto const  dx     = static_cast<float>(state.vel_x * factor * state.distance_scale);
        auto const  dy     = static_cast<float>(state.vel_y * factor * state.distance_scale);

        std::size_t count = 0;

        // ── hi_res_x ──────────────────────────────────────────────────
        if (dx != 0.0f) {
            ctx.event_buffer[count++]  = event_type(EV_REL, REL_WHEEL_HI_RES, static_cast<event_type::value_type>(dx));
            state.acc_x               += dx;
        }

        // ── legacy_x ──────────────────────────────────────────────────
        {
            constexpr auto notch = 120.0f;
            while (state.acc_x >= notch) {
                state.acc_x               -= notch;
                ctx.event_buffer[count++]  = event_type(EV_REL, REL_WHEEL, 1);
            }
            while (state.acc_x <= -notch) {
                state.acc_x               += notch;
                ctx.event_buffer[count++]  = event_type(EV_REL, REL_WHEEL, -1);
            }
        }

        // ── hi_res_y ──────────────────────────────────────────────────
        if (dy != 0.0f) {
            ctx.event_buffer[count++]  = event_type(EV_REL, REL_HWHEEL_HI_RES, static_cast<event_type::value_type>(dy));
            state.acc_y               += dy;
        }

        // ── legacy_y ──────────────────────────────────────────────────
        {
            constexpr auto notch = 120.0f;
            while (state.acc_y >= notch) {
                state.acc_y               -= notch;
                ctx.event_buffer[count++]  = event_type(EV_REL, REL_HWHEEL, 1);
            }
            while (state.acc_y <= -notch) {
                state.acc_y               += notch;
                ctx.event_buffer[count++]  = event_type(EV_REL, REL_HWHEEL, -1);
            }
        }

        // ── SYN_REPORT ────────────────────────────────────────────────
        if (count > 0) {
            ctx.event_buffer[count++] = syn();
        }

        ++state.step;

        if (state.step >= ctx.config->momentum_frames) {
            ctx.momentum_base->clear_animating();
            ctx.momentum_base->reset_mouse_distance();
            return {
              .events       = std::span<event_type const>{ctx.event_buffer.data(), count},
              .next_timeout = basic_scheduler::cancel_tick,
            };
        }

        return {
          .events       = std::span<event_type const>{ctx.event_buffer.data(), count},
          .next_timeout = std::chrono::microseconds{ctx.config->frame_ms * 1000},
        };
    }
} // namespace

// ── basic_momentum_scroll ──────────────────────────────────────────────────

context_action basic_momentum_scroll::handle_scroll_event(basic_scheduler& sched, event_type const& event) noexcept {
    using enum context_action;

    track(event);
    reset_mouse_distance();

    auto const [vel_x, vel_y] = current_velocity();
    if (std::abs(vel_x) < 0.5f && std::abs(vel_y) < 0.5f) {
        return next;
    }

    // Store scheduler reference on first call.
    if (pimpl.get() != nullptr && pimpl->scheduler == nullptr) {
        pimpl->scheduler = &sched;
    }

    // Record mouse origin for distance tracking.
    set_mouse_origin();

    static momentum_context mctx{this, &config, {}, {}};
    mctx.config                    = &config;
    mctx.tick_state                = {};
    mctx.tick_state.vel_x          = vel_x;
    mctx.tick_state.vel_y          = vel_y;
    mctx.tick_state.distance_scale = 1.0f;
    sched.cancel(pimpl->momentum_handle);
    pimpl->momentum_handle = sched.schedule(&momentum_tick, &mctx, std::chrono::microseconds{config.frame_ms * 1000});
    set_animating();

    return next;
}
