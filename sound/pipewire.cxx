// Created by moisrex on 9/18/26.

module;
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#if __has_include(<dlfcn.h>) && __has_include(<pipewire/pipewire.h>)
#    include <dlfcn.h>
#    include <pipewire/pipewire.h>
#    include <poll.h>
#    include <spa/param/audio/format-utils.h>
#    include <spa/param/audio/raw-utils.h>
#    define FS8_HAS_PIPEWIRE 1
#endif

module fs8.mods;

import :backend;
import fs8.log;

using fs8::audio_backend;
using fs8::queue_channels;
using fs8::queue_sample_rate;
using fs8::sample_queue;

// ---------------------------------------------------------------------------
// PipeWire dlsym API
// ---------------------------------------------------------------------------

#if FS8_HAS_PIPEWIRE

namespace {

    using pw_init_fn         = void (*)(int*, char***);
    using pw_loop_new_fn     = struct pw_loop* (*) (const struct spa_dict*);
    using pw_loop_destroy_fn = void (*)(struct pw_loop*);
    using pw_loop_get_fd_fn  = int (*)(struct pw_loop*);
    using pw_loop_iterate_fn = int (*)(struct pw_loop*, int timeout);
    using pw_stream_new_simple_fn =
      struct pw_stream* (*) (struct pw_loop*, char const*, struct pw_properties*, const struct pw_stream_events*, void*);
    using pw_stream_destroy_fn = void (*)(struct pw_stream*);
    using pw_stream_connect_fn =
      int (*)(struct pw_stream*, uint32_t direction, uint32_t target_id, uint32_t flags, const struct spa_pod**, uint32_t n_params);
    using pw_stream_set_active_fn     = int (*)(struct pw_stream*, bool active);
    using pw_stream_dequeue_buffer_fn = struct pw_buffer* (*) (struct pw_stream*);
    using pw_stream_queue_buffer_fn   = int (*)(struct pw_stream*, struct pw_buffer*);
    using pw_properties_new_fn        = struct pw_properties* (*) (char const*, ...);

    struct pipewire_api {
        void* lib = nullptr;

        pw_init_fn                  init                  = nullptr;
        pw_loop_new_fn              loop_new              = nullptr;
        pw_loop_destroy_fn          loop_destroy          = nullptr;
        pw_loop_get_fd_fn           loop_get_fd           = nullptr;
        pw_loop_iterate_fn          loop_iterate          = nullptr;
        pw_stream_new_simple_fn     stream_new_simple     = nullptr;
        pw_stream_destroy_fn        stream_destroy        = nullptr;
        pw_stream_connect_fn        stream_connect        = nullptr;
        pw_stream_set_active_fn     stream_set_active     = nullptr;
        pw_stream_dequeue_buffer_fn stream_dequeue_buffer = nullptr;
        pw_stream_queue_buffer_fn   stream_queue_buffer   = nullptr;
        pw_properties_new_fn        properties_new        = nullptr;

        [[nodiscard]] bool loaded() const noexcept {
            return lib != nullptr;
        }

        bool load() noexcept {
            lib = dlopen("libpipewire-0.3.so.0", RTLD_LAZY | RTLD_LOCAL);
            if (lib == nullptr) {
                lib = dlopen("libpipewire-0.3.so", RTLD_LAZY | RTLD_LOCAL);
            }
            if (lib == nullptr) {
                return false;
            }

#    define LOAD_SYM(member, sym) member = reinterpret_cast<decltype(member)>(dlsym(lib, sym))
            LOAD_SYM(init, "pw_init");
            LOAD_SYM(loop_new, "pw_loop_new");
            LOAD_SYM(loop_destroy, "pw_loop_destroy");
            LOAD_SYM(loop_get_fd, "pw_loop_get_fd");
            LOAD_SYM(loop_iterate, "pw_loop_iterate");
            LOAD_SYM(stream_new_simple, "pw_stream_new_simple");
            LOAD_SYM(stream_destroy, "pw_stream_destroy");
            LOAD_SYM(stream_connect, "pw_stream_connect");
            LOAD_SYM(stream_set_active, "pw_stream_set_active");
            LOAD_SYM(stream_dequeue_buffer, "pw_stream_dequeue_buffer");
            LOAD_SYM(stream_queue_buffer, "pw_stream_queue_buffer");
            LOAD_SYM(properties_new, "pw_properties_new");
#    undef LOAD_SYM

            return init
                   != nullptr
                   && loop_new
                   != nullptr
                   && loop_iterate
                   != nullptr
                   && stream_new_simple
                   != nullptr
                   && stream_connect
                   != nullptr;
        }
    };

    [[nodiscard]] pipewire_api& api() noexcept {
        static pipewire_api instance;
        return instance;
    }

    bool ensure_api_loaded() noexcept {
        auto& a = api();
        if (a.loaded()) {
            return true;
        }
        if (!a.load()) {
            return false;
        }
        a.init(nullptr, nullptr);
        return true;
    }

    // ---------------------------------------------------------------------------
    // PipeWire audio backend (pw_loop driven by io_manager)
    // ---------------------------------------------------------------------------

    struct pipewire_backend final : audio_backend {
        sample_queue      queue{};
        struct pw_loop*   pw_loop_      = nullptr;
        struct pw_stream* pw_stream     = nullptr;
        void**            callback_pack = nullptr;
        int               loop_fd       = -1;

        static void process_cb(void* userdata) noexcept {
            auto& self = *static_cast<pipewire_backend*>(userdata);
            auto& a    = api();

            auto* b = a.stream_dequeue_buffer(self.pw_stream);
            if (b == nullptr) [[unlikely]] {
                return;
            }

            auto* spa_data = &b->buffer->datas[0];

            if (spa_data->data == nullptr) [[unlikely]] {
                a.stream_queue_buffer(self.pw_stream, b);
                return;
            }

            auto* dest = static_cast<std::int16_t*>(spa_data->data);

            auto const capacity_frames = spa_data->maxsize / (sizeof(std::int16_t) * queue_channels);

            auto const requested_frames = static_cast<std::size_t>(b->requested);

            auto const frames = requested_frames != 0 ? std::min(requested_frames, capacity_frames) : capacity_frames;

            auto const count = frames * queue_channels;

            std::array<float, queue_sample_rate * queue_channels> tmp{};
            auto const                                            n      = std::min(count, tmp.size());
            auto const                                            filled = self.queue.pop(std::span<float>{tmp.data(), n});

            auto const* src = tmp.data();
            for (std::size_t i = 0; i < n; ++i) {
                float s = i < filled ? src[i] : 0.0f;
                if (s > 1.0f) {
                    s = 1.0f;
                }
                if (s < -1.0f) {
                    s = -1.0f;
                }
                dest[i] = static_cast<std::int16_t>(s * 32767.0f);
            }

            spa_data->chunk->offset = 0;
            spa_data->chunk->stride = static_cast<std::int32_t>(sizeof(std::int16_t) * queue_channels);
            spa_data->chunk->size   = static_cast<std::uint32_t>(n * sizeof(std::int16_t));

            a.stream_queue_buffer(self.pw_stream, b);
        }

        static void state_changed_cb(
          void* /*userdata*/,
          enum pw_stream_state /*old_state*/,
          enum pw_stream_state new_state,
          char const*          error) noexcept {
            if (new_state == PW_STREAM_STATE_ERROR) {
                fs8::log("sound: PipeWire stream error: {}", error ? error : "unknown");
            }
        }

        void cleanup() noexcept {
            auto& a = api();
            if (pw_stream != nullptr && a.loaded()) {
                a.stream_destroy(pw_stream);
                pw_stream = nullptr;
            }
            if (pw_loop_ != nullptr && a.loaded()) {
                a.loop_destroy(pw_loop_);
                pw_loop_ = nullptr;
            }
            loop_fd = -1;
        }

        bool start() noexcept override {
            if (pw_loop_ != nullptr) {
                return true;
            }

            if (!ensure_api_loaded()) [[unlikely]] {
                return false;
            }
            auto& a = api();

            pw_loop_ = a.loop_new(nullptr);
            if (pw_loop_ == nullptr) [[unlikely]] {
                return false;
            }

            loop_fd = a.loop_get_fd(pw_loop_);
            if (loop_fd < 0) [[unlikely]] {
                cleanup();
                return false;
            }

            auto* props = a.properties_new(
              PW_KEY_MEDIA_TYPE,
              "Audio",
              PW_KEY_MEDIA_CATEGORY,
              "Playback",
              PW_KEY_MEDIA_ROLE,
              "Notification",
              PW_KEY_NODE_NAME,
              "foresight-sound",
              nullptr);

            static pw_stream_events events{};
            events.version       = PW_VERSION_STREAM_EVENTS;
            events.process       = &process_cb;
            events.state_changed = &state_changed_cb;

            pw_stream = a.stream_new_simple(pw_loop_, "foresight-sound", props, &events, this);
            if (pw_stream == nullptr) [[unlikely]] {
                cleanup();
                return false;
            }

            struct spa_audio_info_raw info{};
            info.format      = SPA_AUDIO_FORMAT_S16_LE;
            info.channels    = queue_channels;
            info.rate        = queue_sample_rate;
            info.position[0] = SPA_AUDIO_CHANNEL_FL;
            info.position[1] = SPA_AUDIO_CHANNEL_FR;

            std::byte              pod_buf[1024]{};
            struct spa_pod_builder builder{};
            spa_pod_builder_init(&builder, pod_buf, sizeof(pod_buf));

            struct spa_audio_info ai{};
            ai.media_subtype = SPA_MEDIA_SUBTYPE_raw;
            ai.info.raw      = info;

            auto* format = spa_format_audio_build(&builder, SPA_PARAM_EnumFormat, &ai);
            if (format == nullptr) [[unlikely]] {
                cleanup();
                return false;
            }

            int const conn_result = a.stream_connect(
              pw_stream,
              PW_DIRECTION_OUTPUT,
              PW_ID_ANY,
              PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS,
              const_cast<spa_pod const**>(&format),
              1);

            if (conn_result < 0) [[unlikely]] {
                cleanup();
                return false;
            }

            if (a.stream_set_active) {
                (void) a.stream_set_active(pw_stream, true);
            }

            return true;
        }

        void stop() noexcept override {
            cleanup();
        }

        bool push(std::span<float const> samples) noexcept override {
            return queue.push(samples);
        }

        [[nodiscard]] int watch_fd() const noexcept override {
            return loop_fd;
        }

        [[nodiscard]] int watch_events() const noexcept override {
            return POLLIN;
        }

        ~pipewire_backend() override {
            stop();
            delete[] callback_pack;
            callback_pack = nullptr;
        }
    };

} // anonymous namespace

#endif // FS8_HAS_PIPEWIRE

// ---------------------------------------------------------------------------
// Factory entry point
// ---------------------------------------------------------------------------

fs8::audio_backend_result fs8::detail::try_pipewire() noexcept {
#if FS8_HAS_PIPEWIRE
    if (!ensure_api_loaded()) [[unlikely]] {
        return {};
    }
    auto b = std::make_unique<pipewire_backend>();
    if (b->start()) {
        auto  dispatch_fn = api().loop_iterate;
        auto* loop        = b->pw_loop_;
        auto* pack        = new (std::nothrow) void*[2]{reinterpret_cast<void*>(dispatch_fn), loop};
        if (pack == nullptr) [[unlikely]] {
            return {};
        }
        b->callback_pack = pack;
        return {.backend = std::move(b),
                .on_ready =
                  [](void* ctx) noexcept {
                      auto** pp = static_cast<void**>(ctx);
                      auto   fn = reinterpret_cast<pw_loop_iterate_fn>(pp[0]);
                      auto*  l  = static_cast<struct pw_loop*>(pp[1]);
                      (void) fn(l, 0);
                  },
                .on_ready_ctx = pack};
    }
#endif
    return {};
}
