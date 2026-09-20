// Created by moisrex on 9/18/26.

module;
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <memory>
#include <span>

module fs8.mods;

import :sound;
import :backend;
import :io_manager;
import fs8.context;
import fs8.event;
import fs8.log;

using fs8::audio_backend;
using fs8::basic_io_manager;
using fs8::basic_sound_player;
using fs8::basic_synth;
using fs8::context_action;
using fs8::event_type;
using fs8::io_event;
using fs8::io_fd;
using fs8::make_audio_backend;
using fs8::queue_channels;
using fs8::queue_sample_rate;
using fs8::sound_format;
using fs8::sound_id;

// ---------------------------------------------------------------------------
// basic_sound_player<basic_synth>::impl
// ---------------------------------------------------------------------------

template <>
struct fs8::pimpl_idiom<basic_sound_player<basic_synth>>::impl {
    basic_synth                    gen{};
    std::unique_ptr<audio_backend> backend;
    void (*on_ready)(void* ctx) noexcept = nullptr;
    void* on_ready_ctx                   = nullptr;
    bool  started                        = false;

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
    }

    ~impl() {
        stop();
    }
};

// ---------------------------------------------------------------------------
// basic_synth::render
// ---------------------------------------------------------------------------

void basic_synth::render(sound_id const id, sound_format const fmt, std::span<float> const dest) const noexcept {
    using enum sound_id;

    constexpr float pi = 3.14159265358979323846f;

    struct voice_params {
        float freq;
        float freq_end;
        float duration;
        float attack;
        float decay_pow;
    };

    voice_params v{};
    switch (id) {
        case tick: v = {1800.0f, 1800.0f, 0.020f, 0.001f, 6.0f}; break;
        case press: v = {880.0f, 660.0f, 0.045f, 0.001f, 4.0f}; break;
        case release: v = {660.0f, 440.0f, 0.045f, 0.001f, 4.0f}; break;
        case confirm: v = {740.0f, 1180.0f, 0.090f, 0.002f, 3.0f}; break;
        case error: v = {320.0f, 180.0f, 0.140f, 0.002f, 2.5f}; break;
        case toggle_on: v = {520.0f, 780.0f, 0.070f, 0.002f, 3.5f}; break;
        case toggle_off: v = {780.0f, 520.0f, 0.070f, 0.002f, 3.5f}; break;
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
// basic_sound_player<basic_synth> — play_sound
// ---------------------------------------------------------------------------

template <>
void basic_sound_player<basic_synth>::play_sound(sound_id const id) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
        pimpl->gen = gen_;
    }

    sound_format const fmt{
      .sample_rate = queue_sample_rate,
      .channels    = queue_channels,
    };

    auto const frames = gen_.duration_frames(id, fmt);
    if (frames == 0) [[unlikely]] {
        return;
    }

    constexpr std::size_t max_frames = queue_sample_rate * 150 / 1000;
    if (frames > max_frames) [[unlikely]] {
        return;
    }

    std::array<float, max_frames * queue_channels> samples{};
    gen_.render(id, fmt, std::span<float>{samples.data(), frames * queue_channels});
    auto const total = frames * queue_channels;
    if (pimpl->backend) {
        (void) pimpl->backend->push(std::span<float const>{samples.data(), total});
    }
}

// ---------------------------------------------------------------------------
// basic_sound_player<basic_synth> — do_start
// ---------------------------------------------------------------------------

template <>
context_action basic_sound_player<basic_synth>::do_start(basic_io_manager& io) noexcept {
    using enum context_action;
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
        pimpl->gen = gen_;
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
        if (int const fd = pimpl->backend->watch_fd(); fd >= 0) {
            (void) io.watch(io_fd{.fd = fd, .events = static_cast<io_event>(pimpl->backend->watch_events())}, *pimpl);
        }
        pimpl->started = true;
    }
    return next;
}
