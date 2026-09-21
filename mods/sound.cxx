// Created by moisrex on 9/18/26.

module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <linux/input-event-codes.h>
#include <memory>
#include <span>

module fs8.mods;

import :sound;
import fs8.sound;
import :io_manager;
import fs8.context;
import fs8.event;
import fs8.log;

using fs8::audio_backend;
using fs8::basic_io_manager;
using fs8::basic_sound_player_core;
using fs8::basic_synth;
using fs8::context_action;
using fs8::event_type;
using fs8::io_event;
using fs8::io_fd;
using fs8::make_audio_backend;
using fs8::max_slot_samples;
using fs8::queue_channels;
using fs8::queue_sample_rate;
using fs8::slot_buffer;
using fs8::slot_pool_size;

// ---------------------------------------------------------------------------
// Sound slot (internal to impl)
// ---------------------------------------------------------------------------

namespace {

    struct sound_slot {
        std::array<float, max_slot_samples> buffer{};

        std::size_t total_samples = 0;
        std::size_t read_pos      = 0;

        bool active = false;

        float         gain          = 1.0f;
        float         peak          = 0.0f;
        std::uint64_t start_serial  = 0;
    };

} // anonymous namespace

// ---------------------------------------------------------------------------
// basic_sound_player_core::impl
// ---------------------------------------------------------------------------

template <>
struct fs8::pimpl_idiom<basic_sound_player_core>::impl {
    std::unique_ptr<audio_backend> backend;
    void (*on_ready)(void* ctx) noexcept = nullptr;
    void* on_ready_ctx                   = nullptr;
    bool  started                        = false;
    bool  paused                         = false;

    std::array<sound_slot, slot_pool_size> slots{};
    std::uint64_t                          next_serial = 1;

    // -----------------------------------------------------------------------
    // Slot allocation
    // -----------------------------------------------------------------------

    [[nodiscard]] std::size_t choose_slot() noexcept {
        // Prefer genuinely free slots.
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (!slots[i].active) {
                return i;
            }
        }

        // Steal: lowest peak amplitude, oldest as tie-breaker.
        std::size_t    best        = 0;
        float          best_peak   = slots[0].peak;
        std::uint64_t  best_serial = slots[0].start_serial;

        for (std::size_t i = 1; i < slots.size(); ++i) {
            auto const& s = slots[i];
            if (s.peak < best_peak || (s.peak == best_peak && s.start_serial < best_serial)) {
                best        = i;
                best_peak   = s.peak;
                best_serial = s.start_serial;
            }
        }

        return best;
    }

    // -----------------------------------------------------------------------
    // Fill callback — mixed on demand by the audio backend
    // -----------------------------------------------------------------------

    static std::size_t fill_mixed_audio(void* ctx, std::span<float> dest) noexcept {
        auto& self = *static_cast<fs8::pimpl_idiom<basic_sound_player_core>::impl*>(ctx);

        if (dest.empty()) {
            return 0;
        }

        // Find how many samples remain across all active slots.
        std::size_t max_remaining = 0;
        for (auto const& s : self.slots) {
            if (s.active) {
                max_remaining = std::max(max_remaining, s.total_samples - s.read_pos);
            }
        }
        if (max_remaining == 0) {
            return 0;
        }

        // Mix up to dest.size() samples.
        auto const samples_to_mix = std::min(max_remaining, dest.size());

        std::fill_n(dest.begin(), samples_to_mix, 0.0f);

        for (auto const& s : self.slots) {
            if (!s.active) {
                continue;
            }
            auto const remaining = s.total_samples - s.read_pos;
            auto const n         = std::min(samples_to_mix, remaining);

            for (std::size_t i = 0; i < n; ++i) {
                dest[i] += s.buffer[s.read_pos + i] * s.gain;
            }
        }

        // Clamp to [-1, 1].
        for (std::size_t i = 0; i < samples_to_mix; ++i) {
            float s = dest[i];
            if (s > 1.0f) {
                s = 1.0f;
            }
            if (s < -1.0f) {
                s = -1.0f;
            }
            dest[i] = s;
        }

        // Advance read positions.
        for (auto& s : self.slots) {
            if (!s.active) {
                continue;
            }
            auto const n = std::min(samples_to_mix, s.total_samples - s.read_pos);
            s.read_pos += n;
            if (s.read_pos >= s.total_samples) {
                s.active        = false;
                s.read_pos      = 0;
                s.total_samples = 0;
                s.peak          = 0.0f;
            }
        }

        return samples_to_mix;
    }

    // -----------------------------------------------------------------------
    // IO callback — for ALSA/OSS eventfd wakeup
    // -----------------------------------------------------------------------

    context_action operator()(io_fd& /*io*/) noexcept {
        if (on_ready) {
            on_ready(on_ready_ctx);
        }
        return context_action::next;
    }

    void stop() noexcept {
        if (backend) {
            backend->stop();
        }
        started = false;
        for (auto& s : slots) {
            s.active        = false;
            s.read_pos      = 0;
            s.total_samples = 0;
        }
    }

    ~impl() {
        stop();
    }
};

// ---------------------------------------------------------------------------
// basic_synth::render
// ---------------------------------------------------------------------------

void basic_synth::render(event_type const& event, sound_format const fmt, std::span<float> const dest) const noexcept {
    if (event.type() != EV_KEY || event.value() > 1) [[unlikely]] {
        return;
    }

    constexpr float pi = 3.14159265358979323846f;

    struct voice_params {
        float freq;
        float freq_end;
        float duration;
        float attack;
        float decay_pow;
    };

    // value 0 = release, value 1 = press
    voice_params v{};
    if (event.value() == 1) {
        v = {.freq = 880.0f, .freq_end = 660.0f, .duration = 0.045f, .attack = 0.001f, .decay_pow = 4.0f};
    } else {
        v = {.freq = 660.0f, .freq_end = 440.0f, .duration = 0.045f, .attack = 0.001f, .decay_pow = 4.0f};
    }

    if (fmt.channels == 0 || fmt.sample_rate == 0 || v.duration <= v.attack) [[unlikely]] {
        return;
    }

    auto const frames = dest.size() / static_cast<std::size_t>(fmt.channels);
    if (frames == 0) [[unlikely]] {
        return;
    }

    float const inv_sr  = 1.0f / static_cast<float>(fmt.sample_rate);
    float const inv_n   = 1.0f / static_cast<float>(frames);
    float const attack  = v.attack;
    float const att_inv = attack > 0.0f ? 1.0f / attack : 0.0f;
    float       phase   = 0.0f;

    for (std::size_t i = 0; i < frames; ++i) {
        float const t = static_cast<float>(i) * inv_sr;
        float const u = static_cast<float>(i) * inv_n;

        float const freq = v.freq + (v.freq_end - v.freq) * u;

        phase += 2.0f * pi * freq * inv_sr;
        if (phase > 2.0f * pi) {
            phase -= 2.0f * pi;
        }

        float envelope;
        if (t < attack) {
            envelope = t * att_inv;
        } else {
            float const d = (t - attack) / (v.duration - attack);
            envelope      = d >= 1.0f ? 0.0f : std::pow(1.0f - d, v.decay_pow);
        }

        float const sample = std::sin(phase) * envelope * 0.15f;

        for (uint16_t ch = 0; ch < fmt.channels; ++ch) {
            dest[static_cast<std::size_t>(i) * fmt.channels + ch] = sample;
        }
    }
}

// ---------------------------------------------------------------------------
// basic_sound_player_core — ensure_backend
// ---------------------------------------------------------------------------

context_action basic_sound_player_core::ensure_backend(basic_io_manager& io) noexcept {
    using enum context_action;
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    if (!pimpl->started) {
        auto result         = make_audio_backend();
        pimpl->backend      = std::move(result.backend);
        pimpl->on_ready     = result.on_ready;
        pimpl->on_ready_ctx = result.on_ready_ctx;
        if (!pimpl->backend) [[unlikely]] {
            log("sound: no audio backend available — sound disabled.");
            return next;
        }
        // Register the pull-based mixing callback with the backend.
        pimpl->backend->set_process_callback(&impl::fill_mixed_audio, pimpl.get());
        if (int const fd = pimpl->backend->watch_fd(); fd >= 0) {
            (void) io.watch(io_fd{.fd = fd, .events = static_cast<io_event>(pimpl->backend->watch_events())}, *pimpl);
        }
        pimpl->started    = true;
        pimpl->slots      = {};
        pimpl->next_serial = 1;
    }
    return next;
}

// ---------------------------------------------------------------------------
// basic_sound_player_core — acquire_slot
// ---------------------------------------------------------------------------

slot_buffer basic_sound_player_core::acquire_slot(std::size_t const sample_count) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    if (!pimpl->backend || sample_count == 0 || sample_count > max_slot_samples) [[unlikely]] {
        return {};
    }

    auto const index = pimpl->choose_slot();
    auto& s          = pimpl->slots[index];

    // Prepare the slot for rendering.
    s.active        = false; // hidden until commit
    s.read_pos      = 0;
    s.total_samples = sample_count;
    s.gain          = 1.0f;
    s.peak          = 0.0f;
    s.start_serial  = pimpl->next_serial++;

    return slot_buffer{
      .samples = std::span<float>{s.buffer.data(), sample_count},
      .index   = index,
    };
}

// ---------------------------------------------------------------------------
// basic_sound_player_core — commit_slot
// ---------------------------------------------------------------------------

void basic_sound_player_core::commit_slot(std::size_t const index, std::size_t const sample_count) noexcept {
    if (!pimpl || index >= slot_pool_size || sample_count == 0 || sample_count > max_slot_samples) [[unlikely]] {
        return;
    }

    auto& s = pimpl->slots[index];

    // Compute peak amplitude for slot-stealing heuristics.
    float peak = 0.0f;
    for (std::size_t i = 0; i < sample_count; ++i) {
        float const a = std::abs(s.buffer[i]);
        if (a > peak) {
            peak = a;
        }
    }

    s.peak   = peak;
    s.gain   = 1.0f;
    s.active = true;
}

// ---------------------------------------------------------------------------
// basic_sound_player_core — pause control
// ---------------------------------------------------------------------------

bool basic_sound_player_core::toggle_pause() noexcept {
    if (!pimpl) [[unlikely]] {
        return false;
    }
    return pimpl->paused = !pimpl->paused;
}

void basic_sound_player_core::set_paused(bool const paused) noexcept {
    if (!pimpl) [[unlikely]] {
        return;
    }
    pimpl->paused = paused;
}

bool basic_sound_player_core::is_paused() const noexcept {
    return pimpl && pimpl->paused;
}
