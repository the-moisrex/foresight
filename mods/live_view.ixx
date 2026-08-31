// Created by moisrex on 8/25/26.

module;
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include <vector>
export module fs8.mods:live_view;
import fs8.context;
import fs8.event;
import fs8.lib.evtest;
import fs8.lib.xkb;
import fs8.lib.xkb.event2unicode;
import fs8.traits;
import fs8.translate;
import :sanitizer;

export namespace fs8 {

    /// Format for the live view output: aligned columns with text and hold duration.
    struct [[nodiscard]] live_view_format {
        /// Parse one live-view line. Returns false for non-event lines.
        [[nodiscard]] bool parse(std::string_view line, parsed_evtest_event& out) const noexcept;

        /// Format an event into `buf`. Returns the written slice, or empty on failure.
        [[nodiscard]] std::string_view format(event_type const& event, std::span<char> buf) const noexcept;
    };

    static_assert(EvtestFormat<live_view_format>);

    inline constexpr std::size_t live_view_format_buf_size = 256;

    /// Parse result extended with device_id.
    struct [[nodiscard]] live_view_parse_result {
        user_event event;
        double     time   = 0;
        device_id  source = device_id::none;
    };

    // ── State structs for live_view ──────────────────────────────────────────

    /// Accumulated mouse movement between SYN_REPORTs.
    struct [[nodiscard]] mouse_accum {
        int                       delta_x     = 0;
        int                       delta_y     = 0;
        int                       wheel_hi    = 0; // REL_WHEEL_HI_RES accumulation
        int                       wheel       = 0; // REL_WHEEL accumulation
        int                       hwheel_hi   = 0; // REL_HWHEEL_HI_RES accumulation
        int                       hwheel      = 0; // REL_HWHEEL accumulation
        std::size_t               event_count = 0;
        std::size_t               syn_count   = 0;
        std::chrono::microseconds first_event_time{};
        std::chrono::microseconds last_event_time{};
        float                     prev_dir_x    = 0.f;
        float                     prev_dir_y    = 0.f;
        bool                      has_direction = false;
    };

    /// A key currently held down.
    struct [[nodiscard]] held_key {
        std::uint16_t             code = KEY_MAX;
        std::chrono::microseconds press_time{};
        bool                      has_intervening_events = false;
    };

    /// Per-device state for the live view.
    struct [[nodiscard]] device_live_state {
        mouse_accum                                 mouse{};
        std::unordered_map<std::uint16_t, held_key> held_keys;
        std::optional<xkb::basic_state>             xkb_state;
        sanitizer_issue                             pending_issue = sanitizer_issue::none;
    };

    // ── Live view state ──────────────────────────────────────────────────────

    /// Stateful event processor for the live view. Accumulates mouse movements
    /// by direction, tracks keyboard hold durations, and formats output for
    /// terminal display.
    struct [[nodiscard]] live_view {
      private:
        bool                                             terminal_mode_     = false;
        bool                                             use_ansi_          = false;
        float                                            direction_epsilon_ = 0.0f;
        std::chrono::microseconds                        flush_timeout_{16'000}; // 16ms (~60fps)
        std::unordered_map<device_id, device_live_state> devices_;
        xkb::context                                     xkb_ctx_;
        std::optional<xkb::keymap>                       xkb_keymap_;

      public:
        /// Construct a live view. Detects terminal mode if term_fd is a TTY.
        explicit live_view(bool force_terminal = false) noexcept;

        /// Enable or disable ANSI color output.
        void set_ansi(bool on) noexcept {
            use_ansi_ = on;
        }

        /// Set the direction-change epsilon (dot-product threshold).
        void set_direction_epsilon(float eps) noexcept {
            direction_epsilon_ = eps;
        }

        /// Set the flush timeout (default 1 second).
        void set_flush_timeout(std::chrono::microseconds t) noexcept {
            flush_timeout_ = t;
        }

        /// Whether we're in terminal mode.
        [[nodiscard]] bool is_terminal() const noexcept {
            return terminal_mode_;
        }

        /// Process one event: accumulate mouse, track keys, format and write.
        void process_event(event_type const& event, int fd, sanitizer_issue issue = sanitizer_issue::none);

        /// Flush any accumulated state (e.g. on timeout).
        void flush(int fd);

        /// Get per-device state, creating if needed.
        [[nodiscard]] device_live_state& state_for(device_id id);

      private:
        /// Flush accumulated mouse movement for one device.
        void flush_mouse(device_id id, device_live_state& st, int fd);

        /// Flush keyboard buffer for one device.
        void flush_keyboard(device_id id, device_live_state& st, int fd);

        /// Write a formatted line to fd, with optional \r for terminal live update.
        void write_line(std::string_view line, int fd, bool is_live_status = false);

        /// Format and write a mouse accumulation summary.
        void write_mouse_summary(device_id id, mouse_accum const& m, int fd);

        /// Format and write a diagnostic annotation.
        void write_diagnostic(device_id id, sanitizer_issue issue, event_type const& event, int fd);

        /// Format and write a key event with text column.
        void write_key_event(device_id id, event_type const& event, int fd, char32_t text = U'\0');

        /// Format and write a generic (non-key, non-mouse) event.
        void write_generic_event(device_id id, event_type const& event, int fd);

        /// Check if a mouse direction change occurred.
        [[nodiscard]] bool direction_changed(mouse_accum const& m, int dx, int dy) const noexcept;

        /// Get or initialize XKB state for a device.
        [[nodiscard]] xkb::basic_state& get_xkb_state(device_live_state& st);

        /// Convert a key event to Unicode using XKB.
        [[nodiscard]] char32_t key_to_text(device_live_state& st, event_type const& event);

        /// Format a device-id prefix like "dev3a".
        [[nodiscard]] static std::string_view format_device_id(device_id id, std::span<char> buf) noexcept;

        /// Format a timestamp.
        [[nodiscard]] static std::string_view format_time(event_type const& event, std::span<char> buf) noexcept;

        /// ANSI escape: clear current line.
        static constexpr std::string_view ansi_clear_line = "\033[2K";

        /// ANSI escape: move cursor to beginning of line.
        static constexpr std::string_view ansi_carriage_return = "\r";

        // ── ANSI color codes ─────────────────────────────────────────────
        static constexpr std::string_view ansi_reset         = "\033[0m";
        static constexpr std::string_view ansi_dim           = "\033[2m";
        static constexpr std::string_view ansi_bold          = "\033[1m";
        static constexpr std::string_view ansi_red           = "\033[31m";
        static constexpr std::string_view ansi_green         = "\033[32m";
        static constexpr std::string_view ansi_yellow        = "\033[33m";
        static constexpr std::string_view ansi_blue          = "\033[34m";
        static constexpr std::string_view ansi_magenta       = "\033[35m";
        static constexpr std::string_view ansi_cyan          = "\033[36m";
        static constexpr std::string_view ansi_white         = "\033[37m";
        static constexpr std::string_view ansi_bright_red    = "\033[91m";
        static constexpr std::string_view ansi_bright_green  = "\033[92m";
        static constexpr std::string_view ansi_bright_yellow = "\033[93m";
        static constexpr std::string_view ansi_bright_cyan   = "\033[96m";
    };

    inline constexpr auto default_live_flush_timeout = std::chrono::microseconds{16'000};

    // ── Pipeline mods ────────────────────────────────────────────────────────

    /// Write events to a file descriptor in live-view text format.
    template <EvtestFormat Format = live_view_format>
    struct [[nodiscard]] basic_live_view_output : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        int    file_descriptor = STDOUT_FILENO;
        Format format{};

      public:
        constexpr explicit basic_live_view_output(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        constexpr void set_output(int const inp_fd) noexcept {
            file_descriptor = inp_fd;
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(event_type const& event) const noexcept {
            char       buf[live_view_format_buf_size];
            auto const text = format.format(event, buf);
            if (text.empty()) [[unlikely]] {
                return false;
            }
            return write(file_descriptor, text.data(), text.size()) == static_cast<ssize_t>(text.size());
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool operator()(event_type& event) const noexcept {
            return emit(event);
        }
    };

    template <EvtestFormat Format>
    inline constexpr basic_live_view_output<Format> live_view_output;

    constexpr auto to_live_view = live_view_output<live_view_format>;

    static_assert(OutputModifier<basic_live_view_output<>>, "Must be a output modifier.");

    /// Read live-view-format text from a file descriptor, parse it, and feed
    /// events into the pipeline.
    template <EvtestFormat Format = live_view_format>
    struct [[nodiscard]] basic_from_live_view : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        int                 file_descriptor = STDIN_FILENO;
        Format              format{};
        mutable std::string line_buffer;

        /// Try to parse existing complete lines in the buffer. Returns true
        /// when a line was successfully parsed (event is filled in).
        [[nodiscard]] bool try_parse_buffered(event_type& event) noexcept {
            while (true) {
                auto const newline = line_buffer.find('\n');
                if (newline == std::string::npos) {
                    break;
                }
                std::string_view const line{line_buffer.data(), newline};
                parsed_evtest_event    parsed;
                if (format.parse(line, parsed)) {
                    line_buffer.erase(0, newline + 1);
                    event = event_type{parsed.event};
                    event.source(device_id::stdin);
                    return true;
                }
                line_buffer.erase(0, newline + 1);
            }
            return false;
        }

      public:
        constexpr explicit basic_from_live_view(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        constexpr void set_input(int const inp_fd) noexcept {
            file_descriptor = inp_fd;
        }

        context_action operator()(event_type& event, special_event const& tag) noexcept;
    };

    inline constinit basic_from_live_view<> from_live_view;

} // namespace fs8
