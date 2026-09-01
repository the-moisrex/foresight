module;
#include <cmath>
#include <cstdint>
#include <linux/input-event-codes.h>
module fs8.mods;

using fs8::basic_kalman_filter;
using fs8::basic_lerp;
using fs8::basic_low_pass_filter;

template <>
struct fs8::pimpl_idiom<fs8::basic_lerp>::impl {
    bool         had_movement = false;
    std::int32_t cur_x        = 0;
    std::int32_t cur_y        = 0;

    void accumulate(std::uint16_t const code, basic_lerp::value_type const value) noexcept {
        had_movement = true;
        switch (code) {
            case REL_X: cur_x += value; break;
            case REL_Y: cur_y += value; break;
            default: break;
        }
    }

    bool take_frame() noexcept {
        if (!had_movement) {
            return false;
        }
        had_movement = false;
        return true;
    }

    std::pair<basic_lerp::value_type, basic_lerp::value_type> position() const noexcept {
        return {cur_x, cur_y};
    }

    void reset() noexcept {
        cur_x = 0;
        cur_y = 0;
    }
};

template <>
struct fs8::pimpl_idiom<fs8::basic_low_pass_filter>::impl {
    float        prev_x       = 0.f;
    float        prev_y       = 0.f;
    bool         had_movement = false;
    bool         initialized  = false;
    std::int32_t acc_x        = 0;
    std::int32_t acc_y        = 0;

    void accumulate(std::uint16_t const code, basic_low_pass_filter::value_type const value) noexcept {
        had_movement = true;
        switch (code) {
            case REL_X: acc_x += value; break;
            case REL_Y: acc_y += value; break;
            default: break;
        }
    }

    basic_low_pass_filter::smoothed
    filter_frame(float const alpha, basic_low_pass_filter::value_type const inp_x, basic_low_pass_filter::value_type const inp_y) noexcept {
        if (!had_movement) {
            return {};
        }
        had_movement = false;
        if (inp_x == 0 && inp_y == 0) {
            return {};
        }
        if (!initialized) {
            initialized = true;
            prev_x      = static_cast<float>(inp_x);
            prev_y      = static_cast<float>(inp_y);
        } else {
            prev_x = alpha * static_cast<float>(inp_x) + (1.f - alpha) * prev_x;
            prev_y = alpha * static_cast<float>(inp_y) + (1.f - alpha) * prev_y;
        }
        return basic_low_pass_filter::smoothed{
          .emit = true,
          .x    = static_cast<basic_low_pass_filter::value_type>(std::round(prev_x)),
          .y    = static_cast<basic_low_pass_filter::value_type>(std::round(prev_y)),
        };
    }

    std::pair<basic_low_pass_filter::value_type, basic_low_pass_filter::value_type> position() const noexcept {
        return {acc_x, acc_y};
    }

    void reset() noexcept {
        acc_x = 0;
        acc_y = 0;
    }
};

template <>
struct fs8::pimpl_idiom<fs8::basic_kalman_filter>::impl {
    float        est_x        = 0.f;
    float        est_y        = 0.f;
    float        cov_x        = 0.f;
    float        cov_y        = 0.f;
    bool         had_movement = false;
    bool         initialized  = false;
    std::int32_t acc_x        = 0;
    std::int32_t acc_y        = 0;

    void accumulate(std::uint16_t const code, basic_kalman_filter::value_type const value) noexcept {
        had_movement = true;
        switch (code) {
            case REL_X: acc_x += value; break;
            case REL_Y: acc_y += value; break;
            default: break;
        }
    }

    basic_kalman_filter::smoothed filter_frame(
      float const                           q,
      float const                           r,
      basic_kalman_filter::value_type const inp_x,
      basic_kalman_filter::value_type const inp_y) noexcept {
        if (!had_movement) {
            return {};
        }
        had_movement = false;
        if (inp_x == 0 && inp_y == 0) {
            return {};
        }
        if (!initialized) {
            initialized = true;
            est_x       = static_cast<float>(inp_x);
            est_y       = static_cast<float>(inp_y);
            cov_x       = q;
            cov_y       = q;
            return basic_kalman_filter::smoothed{.emit = true, .x = inp_x, .y = inp_y};
        }
        cov_x           += q;
        cov_y           += q;
        float const k_x  = cov_x / (cov_x + r);
        float const k_y  = cov_y / (cov_y + r);
        est_x           += k_x * (static_cast<float>(inp_x) - est_x);
        est_y           += k_y * (static_cast<float>(inp_y) - est_y);
        cov_x           *= (1.f - k_x);
        cov_y           *= (1.f - k_y);
        return basic_kalman_filter::smoothed{
          .emit = true,
          .x    = static_cast<basic_kalman_filter::value_type>(std::round(est_x)),
          .y    = static_cast<basic_kalman_filter::value_type>(std::round(est_y)),
        };
    }

    std::pair<basic_kalman_filter::value_type, basic_kalman_filter::value_type> position() const noexcept {
        return {acc_x, acc_y};
    }

    void reset() noexcept {
        acc_x = 0;
        acc_y = 0;
    }
};

// basic_lerp forwarding

void basic_lerp::accumulate(std::uint16_t const code, value_type const value) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->accumulate(code, value);
}

bool basic_lerp::take_frame() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return false;
    }
    return pimpl->take_frame();
}

std::pair<basic_lerp::value_type, basic_lerp::value_type> basic_lerp::position() const noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return {0, 0};
    }
    return pimpl->position();
}

void basic_lerp::reset() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }
    pimpl->reset();
}

// basic_low_pass_filter forwarding

void basic_low_pass_filter::accumulate(std::uint16_t const code, value_type const value) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->accumulate(code, value);
}

basic_low_pass_filter::smoothed basic_low_pass_filter::filter_frame(value_type const cur_x, value_type const cur_y) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return {};
    }
    return pimpl->filter_frame(alpha, cur_x, cur_y);
}

std::pair<basic_low_pass_filter::value_type, basic_low_pass_filter::value_type> basic_low_pass_filter::position() const noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return {0, 0};
    }
    return pimpl->position();
}

void basic_low_pass_filter::reset() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }
    pimpl->reset();
}

// basic_kalman_filter forwarding

void basic_kalman_filter::accumulate(std::uint16_t const code, value_type const value) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->accumulate(code, value);
}

basic_kalman_filter::smoothed basic_kalman_filter::filter_frame(value_type const cur_x, value_type const cur_y) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return {};
    }
    return pimpl->filter_frame(q, r, cur_x, cur_y);
}

std::pair<basic_kalman_filter::value_type, basic_kalman_filter::value_type> basic_kalman_filter::position() const noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return {0, 0};
    }
    return pimpl->position();
}

void basic_kalman_filter::reset() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }
    pimpl->reset();
}
