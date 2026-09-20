// Created by moisrex on 9/18/26.

module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <poll.h>
#include <span>
#include <sys/eventfd.h>
#include <unistd.h>

#if __has_include(<alsa/asoundlib.h>) && __has_include(<dlfcn.h>)
#    include <alsa/asoundlib.h>
#    include <dlfcn.h>
#    define FS8_HAS_ALSA 1
#endif

module fs8.mods;

import :backend;
import fs8.log;

using fs8::audio_backend;
using fs8::queue_channels;
using fs8::queue_sample_rate;
using fs8::sample_queue;

// ---------------------------------------------------------------------------
// ALSA dlsym API
// ---------------------------------------------------------------------------

#if FS8_HAS_ALSA

namespace {

    using snd_pcm_open_fn       = int (*)(snd_pcm_t**, char const*, snd_pcm_stream_t, int);
    using snd_pcm_close_fn      = int (*)(snd_pcm_t*);
    using snd_pcm_set_params_fn = int (*)(snd_pcm_t*, snd_pcm_format_t, snd_pcm_access_t, unsigned int, unsigned int, int, unsigned int);
    using snd_pcm_writei_fn     = snd_pcm_sframes_t (*)(snd_pcm_t*, void const*, snd_pcm_uframes_t);
    using snd_pcm_nonblock_fn   = int (*)(snd_pcm_t*, int);
    using snd_pcm_drop_fn       = int (*)(snd_pcm_t*);
    using snd_pcm_recover_fn    = int (*)(snd_pcm_t*, int, int);
    using snd_pcm_wait_fn       = int (*)(snd_pcm_t*, int);
    using snd_strerror_fn       = char const* (*) (int);

    struct alsa_api {
        void* lib = nullptr;

        snd_pcm_open_fn       pcm_open       = nullptr;
        snd_pcm_close_fn      pcm_close      = nullptr;
        snd_pcm_set_params_fn pcm_set_params = nullptr;
        snd_pcm_writei_fn     pcm_writei     = nullptr;
        snd_pcm_nonblock_fn   pcm_nonblock   = nullptr;
        snd_pcm_drop_fn       pcm_drop       = nullptr;
        snd_pcm_recover_fn    pcm_recover    = nullptr;
        snd_pcm_wait_fn       pcm_wait       = nullptr;
        snd_strerror_fn       str_error      = nullptr;

        [[nodiscard]] bool loaded() const noexcept {
            return lib != nullptr;
        }

        bool load() noexcept {
            lib = dlopen("libasound.so.2", RTLD_LAZY | RTLD_LOCAL);
            if (lib == nullptr) {
                lib = dlopen("libasound.so", RTLD_LAZY | RTLD_LOCAL);
            }
            if (lib == nullptr) {
                return false;
            }

#    define LOAD_SYM(member, sym) member = reinterpret_cast<decltype(member)>(dlsym(lib, sym))
            LOAD_SYM(pcm_open, "snd_pcm_open");
            LOAD_SYM(pcm_close, "snd_pcm_close");
            LOAD_SYM(pcm_set_params, "snd_pcm_set_params");
            LOAD_SYM(pcm_writei, "snd_pcm_writei");
            LOAD_SYM(pcm_nonblock, "snd_pcm_nonblock");
            LOAD_SYM(pcm_drop, "snd_pcm_drop");
            LOAD_SYM(pcm_recover, "snd_pcm_recover");
            LOAD_SYM(pcm_wait, "snd_pcm_wait");
            LOAD_SYM(str_error, "snd_strerror");
#    undef LOAD_SYM

            return pcm_open != nullptr && pcm_writei != nullptr && pcm_set_params != nullptr;
        }
    };

    [[nodiscard]] alsa_api& api() noexcept {
        static alsa_api instance;
        return instance;
    }

    // ---------------------------------------------------------------------------
    // ALSA audio backend (driven by io_manager via eventfd)
    // ---------------------------------------------------------------------------

    struct alsa_backend final : audio_backend {
        sample_queue queue{};
        snd_pcm_t*   pcm       = nullptr;
        int          wakeup_fd = -1;

        bool start() noexcept override {
            if (pcm != nullptr) {
                return true;
            }

            auto& a = api();
            if (!a.loaded()) [[unlikely]] {
                return false;
            }

            int rc = a.pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
            if (rc < 0 || pcm == nullptr) [[unlikely]] {
                return false;
            }

            rc = a.pcm_set_params(
              pcm,
              SND_PCM_FORMAT_FLOAT_LE,
              SND_PCM_ACCESS_RW_INTERLEAVED,
              queue_channels,
              queue_sample_rate,
              1,        // allow resampling
              100'000); // latency in us

            if (rc < 0) [[unlikely]] {
                a.pcm_close(pcm);
                pcm = nullptr;
                return false;
            }

            a.pcm_nonblock(pcm, 1);

            wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (wakeup_fd < 0) [[unlikely]] {
                a.pcm_close(pcm);
                pcm = nullptr;
                return false;
            }

            return true;
        }

        void stop() noexcept override {
            if (pcm != nullptr) {
                api().pcm_drop(pcm);
                api().pcm_close(pcm);
                pcm = nullptr;
            }
            if (wakeup_fd >= 0) {
                close(wakeup_fd);
                wakeup_fd = -1;
            }
        }

        bool push(std::span<float const> samples) noexcept override {
            if (!queue.push(samples)) {
                return false;
            }
            if (wakeup_fd >= 0) {
                eventfd_write(wakeup_fd, 1);
            }
            return true;
        }

        [[nodiscard]] int watch_fd() const noexcept override {
            return wakeup_fd;
        }

        [[nodiscard]] int watch_events() const noexcept override {
            return POLLIN;
        }

        void do_on_ready() noexcept {
            if (pcm == nullptr) [[unlikely]] {
                return;
            }
            eventfd_t val;
            eventfd_read(wakeup_fd, &val);

            std::array<float, queue_sample_rate * queue_channels> buf{};
            auto const                                            count = queue.pop(std::span<float>{buf});
            if (count == 0) [[unlikely]] {
                return;
            }

            auto const frames    = count / queue_channels;
            auto       written   = static_cast<std::size_t>(0);
            bool       recovered = false;

            while (written < frames) {
                auto const rc = api().pcm_writei(pcm, buf.data() + written * queue_channels, frames - written);

                if (rc < 0) {
                    if (recovered) {
                        break;
                    }
                    if (api().pcm_recover(pcm, static_cast<int>(rc), 1) < 0) {
                        break;
                    }
                    recovered = true;
                } else if (rc == 0) {
                    break;
                } else {
                    written += static_cast<std::size_t>(rc);
                }
            }
        }

        ~alsa_backend() override {
            stop();
        }
    };

} // anonymous namespace

#endif // FS8_HAS_ALSA

// ---------------------------------------------------------------------------
// Factory entry point
// ---------------------------------------------------------------------------

fs8::audio_backend_result fs8::detail::try_alsa() noexcept {
#if FS8_HAS_ALSA
    auto& a = api();
    if (!a.loaded()) [[unlikely]] {
        if (!a.load()) [[unlikely]] {
            return {};
        }
    }
    auto b = std::make_unique<alsa_backend>();
    if (b->start()) {
        auto* raw = b.get();
        return {.backend = std::move(b),
                .on_ready =
                  [](void* ctx) noexcept {
                      static_cast<alsa_backend*>(ctx)->do_on_ready();
                  },
                .on_ready_ctx = raw};
    }
#endif
    return {};
}
