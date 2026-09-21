// Created by moisrex on 9/18/26.

module;
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <poll.h>
#include <span>
#include <sys/eventfd.h>

#if __has_include(<sys/soundcard.h>) || __has_include(<linux/soundcard.h>)
#    if __has_include(<fcntl.h>) && __has_include(<sys/ioctl.h>) && __has_include(<unistd.h>)
#        include <fcntl.h>
#        include <sys/ioctl.h>
#        include <unistd.h>
#        if __has_include(<sys/soundcard.h>)
#            include <sys/soundcard.h>
#        else
#            include <linux/soundcard.h>
#        endif
#        define FS8_HAS_OSS 1
#    endif
#endif

module fs8.sound;

import fs8.log;

using fs8::audio_backend;
using fs8::queue_channels;
using fs8::queue_sample_rate;
using fs8::sample_queue;

// ---------------------------------------------------------------------------
// OSS audio backend (driven by io_manager via eventfd)
// ---------------------------------------------------------------------------

#if FS8_HAS_OSS

namespace {

    struct oss_backend final : audio_backend {
        sample_queue queue{};
        int          fd        = -1;
        int          wakeup_fd = -1;

        process_fn on_process_fn = nullptr;
        void*      on_process_ctx = nullptr;

        void set_process_callback(process_fn fn, void* ctx) noexcept override {
            on_process_fn = fn;
            on_process_ctx = ctx;
        }

        bool start() noexcept override {
            if (fd >= 0) {
                return true;
            }

            fd = ::open("/dev/dsp", O_WRONLY | O_NONBLOCK);
            if (fd < 0) [[unlikely]] {
                return false;
            }

            // Configure format: 16-bit signed little-endian.
            int format = AFMT_S16_LE;
            if (ioctl(fd, SNDCTL_DSP_SETFMT, &format) < 0) [[unlikely]] {
                ::close(fd);
                fd = -1;
                return false;
            }

            // Configure channels.
            int channels = queue_channels;
            if (ioctl(fd, SNDCTL_DSP_CHANNELS, &channels) < 0) [[unlikely]] {
                ::close(fd);
                fd = -1;
                return false;
            }

            // Configure sample rate.
            int rate = queue_sample_rate;
            if (ioctl(fd, SNDCTL_DSP_SPEED, &rate) < 0) [[unlikely]] {
                ::close(fd);
                fd = -1;
                return false;
            }

            wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (wakeup_fd < 0) [[unlikely]] {
                ::close(fd);
                fd = -1;
                return false;
            }

            return true;
        }

        void stop() noexcept override {
            if (fd >= 0) {
                ::close(fd);
                fd = -1;
            }
            if (wakeup_fd >= 0) {
                ::close(wakeup_fd);
                wakeup_fd = -1;
            }
        }

        bool push(std::span<float const> const samples) noexcept override {
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
            if (fd < 0) [[unlikely]] {
                return;
            }
            eventfd_t val;
            eventfd_read(wakeup_fd, &val);

            std::array<float, queue_sample_rate * queue_channels>   float_buf{};
            std::array<int16_t, queue_sample_rate * queue_channels> int_buf{};

            std::size_t count = 0;
            if (on_process_fn) {
                count = on_process_fn(on_process_ctx, std::span<float>{float_buf});
            } else {
                count = queue.pop(std::span<float>{float_buf});
            }
            if (count == 0) [[unlikely]] {
                return;
            }

            auto const* src = float_buf.data();
            auto*       dst = int_buf.data();
            for (std::size_t i = 0; i < count; ++i) {
                float s = src[i];
                if (s > 1.0f) {
                    s = 1.0f;
                }
                if (s < -1.0f) {
                    s = -1.0f;
                }
                dst[i] = static_cast<int16_t>(s * static_cast<float>(SHRT_MAX));
            }

            auto        written    = std::size_t{0};
            auto const  byte_count = count * sizeof(int16_t);
            auto const* raw        = reinterpret_cast<char const*>(int_buf.data());

            while (written < byte_count) {
                auto const rc = ::write(fd, raw + written, byte_count - written);
                if (rc < 0) [[unlikely]] {
                    break;
                }
                if (rc == 0) [[unlikely]] {
                    break;
                }
                written += static_cast<std::size_t>(rc);
            }
        }

        ~oss_backend() override {
            stop();
        }
    };

} // anonymous namespace

#endif // FS8_HAS_OSS

// ---------------------------------------------------------------------------
// Factory entry point
// ---------------------------------------------------------------------------

fs8::audio_backend_result fs8::detail::try_oss() noexcept {
#if FS8_HAS_OSS
    auto b = std::make_unique<oss_backend>();
    if (b->start()) {
        auto* raw = b.get();
        return {.backend = std::move(b),
                .on_ready =
                  [](void* ctx) noexcept {
                      static_cast<oss_backend*>(ctx)->do_on_ready();
                  },
                .on_ready_ctx = raw};
    }
#endif
    return {};
}
