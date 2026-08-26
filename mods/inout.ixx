// Created by moisrex on 6/9/25.

module;
#include <charconv>
#include <cstdint>
#include <ctime>
#include <linux/uinput.h>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
export module fs8.mods:inout;
import fs8.context;
import fs8.event;
import fs8.lib.evtest;
import fs8.traits;

export namespace fs8 {

    constexpr struct [[nodiscard]] basic_output : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using ev_type    = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        int file_descriptor = STDOUT_FILENO;

      public:
        constexpr explicit basic_output(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        constexpr void set_output(int const inp_fd) noexcept {
            file_descriptor = inp_fd;
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(event_type const& event) const noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(input_event const& event) const noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(ev_type type, code_type code, value_type value) const noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit_syn() const noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool operator()(event_type& event) const noexcept;
    } output;

    static_assert(OutputModifier<basic_output>, "Must be a output modifier.");

    constexpr struct [[nodiscard]] basic_from_input : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        int file_descriptor = STDIN_FILENO;

      public:
        constexpr explicit basic_from_input(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        context_action operator()(event_type& event, load_event_tag) const noexcept;
    } from_input;

    /// Default evtest format: standard evtest text with libevdev annotations.
    struct [[nodiscard]] default_evtest_format {
        /// Parse one evtest text line. Returns false for non-event lines
        /// (headers, SYN_REPORT separators, junk).
        [[nodiscard]] bool parse(std::string_view line, parsed_evtest_event& out) const noexcept;

        /// Format an event into `buf`. Returns the written slice, or empty on failure.
        [[nodiscard]] std::string_view format(event_type const& event, std::span<char> buf) const noexcept;
    };

    static_assert(EvtestFormat<default_evtest_format>);

    /// Maximum length of an evtest-format event line (including trailing newline).
    inline constexpr std::size_t evtest_format_buf_size = 128;

    /// Read evtest-format text from a file descriptor, parse it, and feed events
    /// into the pipeline. Silently skips header/preamble lines and SYN_REPORT
    /// separators.
    template <EvtestFormat Format = default_evtest_format>
    struct [[nodiscard]] basic_from_evtest : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        int                 file_descriptor = STDIN_FILENO;
        Format              format{};
        mutable std::string line_buffer;

      public:
        constexpr explicit basic_from_evtest(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        constexpr void set_input(int const inp_fd) noexcept {
            file_descriptor = inp_fd;
        }

        context_action operator()(event_type& event, load_event_tag) noexcept {
            using enum context_action;

            // Try to parse existing lines in the buffer first.
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
                    return next;
                }
                // Not an event line (header / junk) — skip it and try the next.
                line_buffer.erase(0, newline + 1);
            }

            // Read more data from the fd.
            while (true) {
                auto const buf_size = line_buffer.size();
                auto const cap      = buf_size + 4096;
                line_buffer.resize(cap);
                auto const n = read(file_descriptor, line_buffer.data() + buf_size, 4096);
                line_buffer.resize(buf_size + static_cast<std::size_t>(n));

                if (n == 0) {
                    // EOF — try to flush any remaining partial line.
                    if (!line_buffer.empty()) {
                        parsed_evtest_event parsed;
                        if (format.parse(line_buffer, parsed)) {
                            line_buffer.clear();
                            event = event_type{parsed.event};
                            event.source(device_id::stdin);
                            return next;
                        }
                        line_buffer.clear();
                    }
                    return exit;
                }

                // Parse every complete line in the buffer.
                while (true) {
                    auto const nl = line_buffer.find('\n');
                    if (nl == std::string::npos) {
                        break;
                    }
                    std::string_view const line{line_buffer.data(), nl};
                    parsed_evtest_event    parsed;
                    if (format.parse(line, parsed)) {
                        line_buffer.erase(0, nl + 1);
                        event = event_type{parsed.event};
                        event.source(device_id::stdin);
                        return next;
                    }
                    line_buffer.erase(0, nl + 1);
                }

                // If we got data, loop to try parsing again (we may now have a complete line).
                if (n > 0) {
                    continue;
                }
                // n < 0: read error — treat as ignore.
                return ignore_event;
            }
        }
    };

    inline constinit basic_from_evtest<> from_evtest;

    /// Write events to a file descriptor in evtest text format.
    template <EvtestFormat Format = default_evtest_format>
    struct [[nodiscard]] basic_evtest_output : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using ev_type    = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        int    file_descriptor = STDOUT_FILENO;
        Format format{};

      public:
        constexpr explicit basic_evtest_output(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        constexpr void set_output(int const inp_fd) noexcept {
            file_descriptor = inp_fd;
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(event_type const& event) const noexcept {
            char       buf[evtest_format_buf_size];
            auto const text = format.format(event, buf);
            if (text.empty()) [[unlikely]] {
                return false;
            }
            return write(file_descriptor, text.data(), text.size()) == static_cast<ssize_t>(text.size());
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(input_event const& event) const noexcept {
            return emit(event_type{event});
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(ev_type const type, code_type const code, value_type const value) const noexcept {
            return emit(event_type{type, code, value});
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit_syn() const noexcept {
            return emit(event_type{EV_SYN, SYN_REPORT, 0});
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool operator()(event_type& event) const noexcept {
            return emit(event);
        }
    };

    template <EvtestFormat Format>
    inline constexpr basic_evtest_output<Format> evtest_output;

    constexpr auto to_evtest = evtest_output<default_evtest_format>;

    static_assert(OutputModifier<basic_evtest_output<>>, "Must be a output modifier.");

} // namespace fs8
