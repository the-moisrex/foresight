// Created by moisrex on 9/18/26.

module;
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

export module fs8.sound;

// DSP helpers and synth parameter tables live in partitions of this module;
// GCC cannot import another module's partition directly (`import M:P;`), so
// re-export them here.
export import :dsp;
export import :chime_data;
export import :bucklespring_data;
export import :modelf_data;
export import :linear_data;
export import :topre_data;
export import :typewriter_data;
export import :mx_blue_data;
export import :alps_data;

export namespace fs8 {

    /// Audio output backend interface.
    ///
    /// Backends are driven by the pipeline's io_manager: the mod registers
    /// the fd returned by `watch_fd()` and, when it fires, calls the
    /// on_ready callback.  The mod also calls `push()` to enqueue samples
    /// from the pipeline thread.  For backends that need an explicit wakeup
    /// (ALSA, OSS), the backend creates its own eventfd and returns it from
    /// `watch_fd()`.
    struct [[nodiscard]] audio_backend {
        audio_backend()                                = default;
        audio_backend(audio_backend const&)            = delete;
        audio_backend& operator=(audio_backend const&) = delete;
        audio_backend(audio_backend&&)                 = delete;
        audio_backend& operator=(audio_backend&&)      = delete;
        virtual ~audio_backend()                       = default;

        /// Open the audio device (no threads created).
        virtual bool start() noexcept = 0;

        /// Close the audio device.
        virtual void stop() noexcept = 0;

        /// Non-blocking push of interleaved samples into the SPSC queue.
        virtual bool push(std::span<float const> samples) noexcept = 0;

        /// Set a pull-based audio callback.  When set, the backend calls
        /// this instead of popping from the SPSC queue.  Returns the number
        /// of float samples written to dest.
        using process_fn = std::size_t (*)(void* ctx, std::span<float> dest) noexcept;

        virtual void set_process_callback(process_fn /*fn*/, void* /*ctx*/) noexcept {}

        /// fd to watch with io_manager (-1 = no fd to watch).
        [[nodiscard]] virtual int watch_fd() const noexcept {
            return -1;
        }

        /// poll events to watch for on watch_fd().
        [[nodiscard]] virtual int watch_events() const noexcept {
            return 0;
        }
    };

    inline constexpr uint32_t queue_sample_rate = 48'000;
    inline constexpr uint16_t queue_channels    = 2;

    /// Raw sample count; round up to the next power of two for bitmask access.
    inline constexpr std::size_t queue_raw_count = static_cast<std::size_t>(queue_sample_rate) * queue_channels;
    inline constexpr std::size_t queue_capacity  = [] {
        std::size_t v = 1;
        while (v < queue_raw_count) {
            v <<= 1u;
        }
        return v;
    }();
    inline constexpr std::size_t queue_mask = queue_capacity - 1;

    /// SPSC lock-free sample queue with power-of-two capacity.
    ///
    /// Single producer (pipeline thread calling push) and single consumer
    /// (RT thread calling pop).  The power-of-two capacity allows replacing
    /// modulo with a bitmask for faster index computation.
    struct [[nodiscard]] sample_queue {
        [[nodiscard]] std::size_t available_write() const noexcept {
            auto const w = write_pos_.load(std::memory_order_relaxed);
            auto const r = read_pos_.load(std::memory_order_acquire);
            return queue_capacity - (w - r);
        }

        /// Push interleaved samples into the queue.  Returns false if the
        /// queue is full (caller must retry later).
        bool push(std::span<float const> samples) noexcept {
            if (samples.size() > available_write()) [[unlikely]] {
                return false;
            }
            auto const w    = write_pos_.load(std::memory_order_relaxed);
            auto const idx  = w & queue_mask;
            auto const head = queue_capacity - idx;
            auto*      d    = data_.data(); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

            if (samples.size() <= head) [[likely]] {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                std::memcpy(d + idx, samples.data(), samples.size() * sizeof(float));
            } else {
                // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                std::memcpy(d + idx, samples.data(), head * sizeof(float));
                std::memcpy(d, samples.data() + head, (samples.size() - head) * sizeof(float));
                // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            }
            write_pos_.store(w + samples.size(), std::memory_order_release);
            return true;
        }

        /// Pop up to destination.size() samples into the destination span.
        /// Returns the number of samples actually popped.
        std::size_t pop(std::span<float> destination) noexcept {
            auto const r     = read_pos_.load(std::memory_order_relaxed);
            auto const w     = write_pos_.load(std::memory_order_acquire);
            auto const count = std::min(destination.size(), w - r);
            if (count == 0) [[unlikely]] {
                return 0;
            }
            auto const idx  = r & queue_mask;
            auto const head = queue_capacity - idx;
            auto*      d    = data_.data(); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

            if (count <= head) [[likely]] {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                std::memcpy(destination.data(), d + idx, count * sizeof(float));
            } else {
                // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                std::memcpy(destination.data(), d + idx, head * sizeof(float));
                std::memcpy(destination.data() + head, d, (count - head) * sizeof(float));
                // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            }
            read_pos_.store(r + count, std::memory_order_release);
            return count;
        }

      private:
        std::atomic<std::size_t>          read_pos_{0};
        std::atomic<std::size_t>          write_pos_{0};
        std::array<float, queue_capacity> data_{};
    };

    /// The result of creating an audio backend: the backend and its
    /// on_ready callback (called when watch_fd() is ready).
    struct [[nodiscard]] audio_backend_result {
        std::unique_ptr<audio_backend> backend;
        void (*on_ready)(void* ctx) noexcept = nullptr;
        void* on_ready_ctx                   = nullptr;
    };

    /// Create the best available audio backend.
    ///
    /// Tries PipeWire, ALSA, OSS in order.  Returns {nullptr, {}} if none
    /// are available (sound will be disabled).
    [[nodiscard]] audio_backend_result make_audio_backend() noexcept;

} // namespace fs8

// Backend try-functions: declared here (not exported), defined in
// each backend .cxx.  Only visible within the fs8.mods module.
namespace fs8::detail {
    [[nodiscard]] audio_backend_result try_pipewire() noexcept;
    [[nodiscard]] audio_backend_result try_alsa() noexcept;
    [[nodiscard]] audio_backend_result try_oss() noexcept;
} // namespace fs8::detail
