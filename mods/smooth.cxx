module;
#include <cmath>
module fs8.mods.smooth;
import fs8.pimpl;

using fs8::basic_kalman_filter;
using fs8::basic_lerp;
using fs8::basic_low_pass_filter;

template <>
struct fs8::pimpl_idiom<fs8::basic_lerp>::impl {
    bool had_movement = false;

    void mark_movement() noexcept {
        had_movement = true;
    }

    bool take_frame() noexcept {
        if (!had_movement) {
            return false;
        }
        had_movement = false;
        return true;
    }
};

template <>
struct fs8::pimpl_idiom<fs8::basic_low_pass_filter>::impl {
    float prev_x       = 0.f;
    float prev_y       = 0.f;
    bool  had_movement = false;
    bool  initialized  = false;

    void mark_movement() noexcept {
        had_movement = true;
    }

    basic_low_pass_filter::smoothed
    filter_frame(float const alpha, basic_low_pass_filter::value_type const cur_x, basic_low_pass_filter::value_type const cur_y) noexcept {
        if (!had_movement) {
            return {};
        }
        had_movement = false;
        if (cur_x == 0 && cur_y == 0) {
            return {};
        }
        if (!initialized) {
            initialized = true;
            prev_x      = static_cast<float>(cur_x);
            prev_y      = static_cast<float>(cur_y);
        } else {
            prev_x = alpha * static_cast<float>(cur_x) + (1.f - alpha) * prev_x;
            prev_y = alpha * static_cast<float>(cur_y) + (1.f - alpha) * prev_y;
        }
        return basic_low_pass_filter::smoothed{
          .emit = true,
          .x    = static_cast<basic_low_pass_filter::value_type>(std::round(prev_x)),
          .y    = static_cast<basic_low_pass_filter::value_type>(std::round(prev_y)),
        };
    }
};

template <>
struct fs8::pimpl_idiom<fs8::basic_kalman_filter>::impl {
    float est_x        = 0.f;
    float est_y        = 0.f;
    float cov_x        = 0.f;
    float cov_y        = 0.f;
    bool  had_movement = false;
    bool  initialized  = false;

    void mark_movement() noexcept {
        had_movement = true;
    }

    basic_kalman_filter::smoothed filter_frame(
      float const                           q,
      float const                           r,
      basic_kalman_filter::value_type const cur_x,
      basic_kalman_filter::value_type const cur_y) noexcept {
        if (!had_movement) {
            return {};
        }
        had_movement = false;
        if (cur_x == 0 && cur_y == 0) {
            return {};
        }
        if (!initialized) {
            initialized = true;
            est_x       = static_cast<float>(cur_x);
            est_y       = static_cast<float>(cur_y);
            cov_x       = q;
            cov_y       = q;
            return basic_kalman_filter::smoothed{.emit = true, .x = cur_x, .y = cur_y};
        }
        cov_x           += q;
        cov_y           += q;
        float const k_x  = cov_x / (cov_x + r);
        float const k_y  = cov_y / (cov_y + r);
        est_x           += k_x * (static_cast<float>(cur_x) - est_x);
        est_y           += k_y * (static_cast<float>(cur_y) - est_y);
        cov_x           *= (1.f - k_x);
        cov_y           *= (1.f - k_y);
        return basic_kalman_filter::smoothed{
          .emit = true,
          .x    = static_cast<basic_kalman_filter::value_type>(std::round(est_x)),
          .y    = static_cast<basic_kalman_filter::value_type>(std::round(est_y)),
        };
    }
};

void basic_lerp::mark_movement() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->mark_movement();
}

bool basic_lerp::take_frame() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return false;
    }
    return pimpl->take_frame();
}

void basic_low_pass_filter::mark_movement() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->mark_movement();
}

basic_low_pass_filter::smoothed basic_low_pass_filter::filter_frame(value_type const cur_x, value_type const cur_y) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return {};
    }
    return pimpl->filter_frame(alpha, cur_x, cur_y);
}

void basic_kalman_filter::mark_movement() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->mark_movement();
}

basic_kalman_filter::smoothed basic_kalman_filter::filter_frame(value_type const cur_x, value_type const cur_y) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return {};
    }
    return pimpl->filter_frame(q, r, cur_x, cur_y);
}
