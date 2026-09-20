// Created by moisrex on 9/18/26.

module;
#include <memory>

module fs8.sound;

import fs8.log;

using fs8::audio_backend;
using fs8::audio_backend_result;

audio_backend_result fs8::make_audio_backend() noexcept {
    if (auto r = fs8::detail::try_pipewire(); r.backend) {
        log("sound: using PipeWire backend");
        return r;
    }
    if (auto r = fs8::detail::try_alsa(); r.backend) {
        log("sound: using ALSA backend");
        return r;
    }
    if (auto r = fs8::detail::try_oss(); r.backend) {
        log("sound: using OSS backend");
        return r;
    }
    return {};
}
