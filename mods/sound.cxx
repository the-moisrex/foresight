// Created by moisrex on 9/18/26.

module;
#include <cmath>
#include <cstddef>
#include <cstdint>
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
using fs8::queue_channels;
using fs8::queue_sample_rate;
using fs8::sound_format;

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
        if (int const fd = pimpl->backend->watch_fd(); fd >= 0) {
            (void) io.watch(io_fd{.fd = fd, .events = static_cast<io_event>(pimpl->backend->watch_events())}, *pimpl);
        }
        pimpl->started = true;
    }
    return next;
}

// ---------------------------------------------------------------------------
// basic_sound_player_core — push_samples
// ---------------------------------------------------------------------------

void basic_sound_player_core::push_samples(std::span<float const> const samples) noexcept {
    if (pimpl && pimpl->backend) {
        (void) pimpl->backend->push(samples);
    }
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
